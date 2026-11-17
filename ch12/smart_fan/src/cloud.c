// Wi-Fi and Firebase (REST API over HTTPS) for the smart fan.
//   Sign in:  POST identitytoolkit.googleapis.com ... signInWithPassword
//   Read:     GET  /fan/desired.json?auth=<token>
//   Write:    PATCH /fan.json?auth=<token>   {"desired":N,"reported":N}
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "secrets.h"
#include "fan_common.h"
#include "cloud.h"

static const char *TAG = "cloud";

#define WIFI_UP_BIT (1 << 0)
static EventGroupHandle_t s_events;

static struct { char buf[4096]; size_t len; } s_rb;   // HTTP response body
static char s_url[1800];
static char s_token[1536];
static int64_t s_token_expiry_us = 0;
static esp_http_client_handle_t s_rtdb_client = NULL;   // kept open between requests

/* ---------------------------------------------------------------- Wi-Fi */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reason %d", d->reason);
        xEventGroupClearBits(s_events, WIFI_UP_BIT);
        vTaskDelay(pdMS_TO_TICKS(1000));   // do not retry in a tight loop
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Connected, IP: " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_events, WIFI_UP_BIT);
    }
}

static void wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Connecting to '%s' (2.4 GHz only)", WIFI_SSID);
}

static void time_sync(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&cfg);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK) {
        ESP_LOGI(TAG, "Time synchronized");
    } else {
        ESP_LOGW(TAG, "Time sync failed, continuing anyway");
    }
}

/* ----------------------------------------------------------------- HTTP */

static esp_err_t http_evt(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        size_t room = sizeof(s_rb.buf) - 1 - s_rb.len;
        size_t n = (size_t)evt->data_len < room ? (size_t)evt->data_len : room;
        memcpy(s_rb.buf + s_rb.len, evt->data, n);
        s_rb.len += n;
        s_rb.buf[s_rb.len] = '\0';
    }
    return ESP_OK;
}

// Runs one request. Returns the HTTP status, or -1 on a transport error.
// keep = true keeps the connection open for the next request (faster).
static int http_do(esp_http_client_handle_t *handle, bool keep,
                   esp_http_client_method_t method, const char *url, const char *body)
{
    s_rb.len = 0;
    s_rb.buf[0] = '\0';

    if (*handle == NULL) {
        esp_http_client_config_t cfg = {
            .url = url,
            .event_handler = http_evt,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 10000,
            .keep_alive_enable = true,
        };
        *handle = esp_http_client_init(&cfg);
        if (*handle == NULL) return -1;
    } else {
        esp_http_client_set_url(*handle, url);
    }

    esp_http_client_set_method(*handle, method);
    if (body) {
        esp_http_client_set_header(*handle, "Content-Type", "application/json");
        esp_http_client_set_post_field(*handle, body, strlen(body));
    } else {
        esp_http_client_set_post_field(*handle, NULL, 0);
    }

    esp_err_t err = esp_http_client_perform(*handle);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(*handle) : -1;
    if (err != ESP_OK) ESP_LOGW(TAG, "HTTP error: %s", esp_err_to_name(err));

    if (!keep || err != ESP_OK) {
        esp_http_client_cleanup(*handle);
        *handle = NULL;
    }
    return status;
}

// Finds "key": "value" in a JSON text and copies the value. No JSON library needed.
static bool json_string(const char *json, const char *key, char *out, size_t cap)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p = strchr(p, '"');
    if (!p) return false;
    p++;
    const char *e = strchr(p, '"');
    if (!e) return false;
    size_t n = (size_t)(e - p);
    if (n >= cap) return false;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

/* ------------------------------------------------------------- Firebase */

static bool token_valid(void)
{
    return s_token[0] != '\0' && esp_timer_get_time() < s_token_expiry_us;
}

static bool firebase_sign_in(void)
{
    char body[320];
    snprintf(s_url, sizeof(s_url),
             "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=%s", API_KEY);
    snprintf(body, sizeof(body),
             "{\"email\":\"%s\",\"password\":\"%s\",\"returnSecureToken\":true}", USER_EMAIL, USER_PASSWORD);

    esp_http_client_handle_t h = NULL;
    int status = http_do(&h, false, HTTP_METHOD_POST, s_url, body);
    if (status != 200) {
        ESP_LOGE(TAG, "Sign-in failed, HTTP %d: %s", status, s_rb.buf);   // 400 = wrong email, password or API key
        return false;
    }
    if (!json_string(s_rb.buf, "idToken", s_token, sizeof(s_token))) {
        ESP_LOGE(TAG, "Sign-in reply has no idToken");
        return false;
    }
    char expires[16];
    int secs = 3600;
    if (json_string(s_rb.buf, "expiresIn", expires, sizeof(expires))) secs = atoi(expires);
    s_token_expiry_us = esp_timer_get_time() + (int64_t)(secs > 600 ? secs - 300 : secs / 2) * 1000000LL;
    ESP_LOGI(TAG, "Signed in to Firebase");
    return true;
}

// Returns 0 = value read, 1 = no value yet, -1 = error.
static int rtdb_get_desired(int *out)
{
    snprintf(s_url, sizeof(s_url), "https://%s/fan/desired.json?auth=%s", DATABASE_URL, s_token);
    int status = http_do(&s_rtdb_client, true, HTTP_METHOD_GET, s_url, NULL);
    if (status == 401) {
        ESP_LOGW(TAG, "HTTP 401: token expired, or the database rules deny access");
        s_token[0] = '\0';   // force a new sign-in
        return -1;
    }
    if (status != 200) return -1;
    if (strncmp(s_rb.buf, "null", 4) == 0) return 1;
    *out = atoi(s_rb.buf);
    return 0;
}

static bool rtdb_publish(int level)
{
    char body[64];
    snprintf(s_url, sizeof(s_url), "https://%s/fan.json?auth=%s", DATABASE_URL, s_token);
    snprintf(body, sizeof(body), "{\"desired\":%d,\"reported\":%d}", level, level);
    int status = http_do(&s_rtdb_client, true, HTTP_METHOD_PATCH, s_url, body);
    if (status == 401) s_token[0] = '\0';
    if (status != 200) ESP_LOGW(TAG, "Publish failed, HTTP %d", status);
    return status == 200;
}

/* ------------------------------------------------------------ cloud task */

static void cloud_task(void *arg)
{
    xEventGroupWaitBits(s_events, WIFI_UP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    time_sync();

    int pending = fan_get_level();   // first publish: make Firebase match the device

    for (;;) {
        if (!(xEventGroupGetBits(s_events) & WIFI_UP_BIT)) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!token_valid()) {
            if (!firebase_sign_in()) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            ESP_LOGI(TAG, "Firebase ready. Waiting for buttons or web commands.");
        }

        // Wait up to 1 s for a level to publish; if none arrives, check the web page's request.
        int lvl;
        if (xQueueReceive(fan_pub_queue, &lvl, pdMS_TO_TICKS(1000)) == pdTRUE) pending = lvl;

        if (pending >= 0) {
            if (rtdb_publish(pending)) pending = -1;
            continue;
        }

        int d;
        if (rtdb_get_desired(&d) == 0 && uxQueueMessagesWaiting(fan_pub_queue) == 0) {
            int want = fan_normalize_level(d);
            if (want != fan_get_level()) {
                fan_cmd_t cmd = { .level = want, .src = SRC_CLOUD };
                xQueueSend(fan_cmd_queue, &cmd, 0);
            }
        }
    }
}

void cloud_start(void)
{
    s_events = xEventGroupCreate();
    wifi_start();
    xTaskCreate(cloud_task, "cloud_task", 12288, NULL, 4, NULL);
}