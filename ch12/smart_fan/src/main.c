// Voice-controlled smart fan: ESP32 + ESP-IDF + Firebase Realtime Database (REST)
//
// Tasks:  button_task -> fan_cmd_queue -> fan_task -> relays
//         cloud_task  -> fan_cmd_queue (commands from the web page)
//         fan_task    -> fan_pub_queue -> cloud_task (publish the new level)
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "fan_common.h"
#include "cloud.h"

static const char *TAG = "fan";

// ---- Inputs: buttons to GND (internal pull-ups) ----
#define NUM_BUTTONS 4
static const gpio_num_t BTN_PINS[NUM_BUTTONS] = {GPIO_NUM_19, GPIO_NUM_18, GPIO_NUM_5, GPIO_NUM_21};
static const int BTN_LEVELS[NUM_BUTTONS] = {0, 33, 66, 100};
static const char *BTN_NAMES[NUM_BUTTONS] = {"OFF (GPIO19)", "LOW (GPIO18)", "NORMAL (GPIO5)", "HIGH (GPIO21)"};

// ---- Outputs: relay inputs ----
#define NUM_RELAYS 3
static const gpio_num_t RELAY_PINS[NUM_RELAYS] = {GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_25};  // Module A IN1, Module A IN2, Module B IN1
static const int RELAY_LEVELS[NUM_RELAYS] = {33, 66, 100};

// true  = relay switches ON when its input pin is pulled LOW. OFF lets the pin float,
//         because a 3.3 V HIGH cannot always switch a 5 V module fully off.
// false = relay switches ON when its input pin is HIGH.
// Order: Module A relay 1, Module A relay 2, Module B relay 1.
static const bool RELAY_ACTIVE_LOW[NUM_RELAYS] = {true, true, true};

#define RELAY_SELF_TEST 0      // 1 = cycle the relays once at boot (33, 66, 100, off)
#define DEBOUNCE_SAMPLES 5     // 5 samples x 10 ms = a press must be stable for 50 ms

QueueHandle_t fan_cmd_queue;
QueueHandle_t fan_pub_queue;
static volatile int s_level = 0;   // written only by fan_task

int fan_get_level(void)
{
    return s_level;
}

static void set_relay(int i, bool on)
{
    gpio_num_t pin = RELAY_PINS[i];
    if (RELAY_ACTIVE_LOW[i]) {
        if (on) {
            gpio_set_level(pin, 0);
            gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        } else {
            gpio_set_direction(pin, GPIO_MODE_INPUT);   // float: the module's own pull-up keeps it off
        }
    } else {
        gpio_set_level(pin, on ? 1 : 0);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    }
}

static void apply_level(int level)
{
    for (int i = 0; i < NUM_RELAYS; i++) set_relay(i, false);
    vTaskDelay(pdMS_TO_TICKS(50));   // break before make, so two relays are never on together
    for (int i = 0; i < NUM_RELAYS; i++) {
        if (RELAY_LEVELS[i] == level) set_relay(i, true);
    }
    s_level = level;
    ESP_LOGI(TAG, "Fan level: %d", level);
}

static void gpio_setup(void)
{
    uint64_t btn_mask = 0;
    for (int i = 0; i < NUM_BUTTONS; i++) btn_mask |= 1ULL << BTN_PINS[i];
    gpio_config_t btn = {
        .pin_bit_mask = btn_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    for (int i = 0; i < NUM_RELAYS; i++) {
        gpio_config_t rel = {
            .pin_bit_mask = 1ULL << RELAY_PINS[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&rel));
        set_relay(i, false);
    }
}

// Reads the buttons every 10 ms. A press must be stable for 50 ms (debounce).
static void button_task(void *arg)
{
    int last[NUM_BUTTONS], stable[NUM_BUTTONS], count[NUM_BUTTONS];
    for (int i = 0; i < NUM_BUTTONS; i++) { last[i] = 1; stable[i] = 1; count[i] = 0; }

    for (;;) {
        for (int i = 0; i < NUM_BUTTONS; i++) {
            int raw = gpio_get_level(BTN_PINS[i]);
            if (raw == last[i]) {
                if (count[i] < DEBOUNCE_SAMPLES) count[i]++;
            } else {
                last[i] = raw;
                count[i] = 0;
            }
            if (count[i] >= DEBOUNCE_SAMPLES && raw != stable[i]) {
                stable[i] = raw;
                if (raw == 0) {   // pull-up, so 0 = pressed
                    ESP_LOGI(TAG, "Button %s pressed", BTN_NAMES[i]);
                    fan_cmd_t cmd = { .level = BTN_LEVELS[i], .src = SRC_BUTTON };
                    xQueueSend(fan_cmd_queue, &cmd, 0);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// The only task that touches the relays. Applies each command, then asks the
// cloud task to publish the new level.
static void fan_task(void *arg)
{
    fan_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(fan_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            apply_level(cmd.level);
            xQueueOverwrite(fan_pub_queue, &cmd.level);   // only the latest level matters
        }
    }
}

#if RELAY_SELF_TEST
static void relay_self_test(void)
{
    ESP_LOGI(TAG, "Relay self-test: 33, 66, 100, off");
    const int steps[4] = {33, 66, 100, 0};
    for (int i = 0; i < 4; i++) {
        apply_level(steps[i]);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}
#endif

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    gpio_setup();
#if RELAY_SELF_TEST
    relay_self_test();
#endif

    fan_cmd_queue = xQueueCreate(8, sizeof(fan_cmd_t));
    fan_pub_queue = xQueueCreate(1, sizeof(int));

    xTaskCreate(fan_task, "fan_task", 4096, NULL, 5, NULL);
    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
    cloud_start();   // Wi-Fi + cloud task
}