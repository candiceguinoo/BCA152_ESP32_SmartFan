// Voice-controlled smart fan: ESP32 + Firebase Realtime Database
// Relay inputs are driven directly from the ESP32 pins (no logic converter).
// Database layout:  /fan/desired  (set by web page / voice)
//                   /fan/reported (set by this device)
#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include "secrets.h"

// ---- Inputs: buttons to GND (internal pull-ups) ----
const uint8_t BTN_PINS[4]   = {19, 18, 5, 21};   // OFF, 33%, 66%, 100%
const int     BTN_LEVELS[4] = {0, 33, 66, 100};

// ---- Outputs: relay inputs ----
const uint8_t RELAY_PINS[3]   = {32, 33, 25};    // Module A IN1, Module A IN2, Module B IN1
const int     RELAY_LEVELS[3] = {33, 66, 100};

// true  = this relay switches ON when its input pin is pulled LOW (most relay modules).
//         OFF is done by letting the pin float, because a 3.3 V HIGH cannot always
//         switch a 5 V module fully off.
// false = this relay switches ON when its input pin is HIGH.
// Order: Module A relay 1, Module A relay 2, Module B relay 1.
// If a relay is lit at Off, or never lights, flip its value here.
const bool RELAY_ACTIVE_LOW[3] = {true, true, true};

// Set to 1 to cycle the relays once at boot (33, 66, 100, off). Use 0 for normal running.
#define RELAY_SELF_TEST 0

// Set to 1 to print a line whenever a button changes between PRESSED and released.
// At power-up all four should say "released". Use 0 for normal running.
#define DEBUG_BUTTONS 1

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

int currentLevel = 0;
bool firstSync = false;
bool pendingPublish = false;
unsigned long lastPoll = 0;
bool lastBtn[4] = {true, true, true, true};
unsigned long lastChange[4] = {0, 0, 0, 0};

// 0 -> 0, 1..33 -> 33, 34..66 -> 66, above 66 -> 100 (same as the Lambda)
int normalizeLevel(int v) {
  if (v <= 0) return 0;
  if (v <= 33) return 33;
  if (v <= 66) return 66;
  return 100;
}

void setRelay(int i, bool on) {
  if (RELAY_ACTIVE_LOW[i]) {
    if (on) {
      pinMode(RELAY_PINS[i], OUTPUT);
      digitalWrite(RELAY_PINS[i], LOW);
    } else {
      pinMode(RELAY_PINS[i], INPUT);   // float: the module's own pull-up keeps it off
    }
  } else {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], on ? HIGH : LOW);
  }
}

void applyLevel(int level) {
  for (int i = 0; i < 3; i++) setRelay(i, false);
  delay(50);  // break before make, so two relays are never on together
  for (int i = 0; i < 3; i++) {
    if (RELAY_LEVELS[i] == level) setRelay(i, true);
  }
  currentLevel = level;
  Serial.printf("Fan level: %d\n", level);
}

bool publishLevel(int level) {
  if (!Firebase.ready()) return false;
  FirebaseJson json;
  json.set("desired", level);   // keep desired in sync so a button press isn't reverted
  json.set("reported", level);
  if (!Firebase.RTDB.updateNode(&fbdo, "/fan", &json)) {
    Serial.printf("publish failed: %s\n", fbdo.errorReason().c_str());
    return false;
  }
  return true;
}

void handleButtons() {
  for (int i = 0; i < 4; i++) {
    bool state = digitalRead(BTN_PINS[i]);
    if (state != lastBtn[i] && millis() - lastChange[i] > 150) {
      lastChange[i] = millis();
      lastBtn[i] = state;
      if (state == LOW) {  // pressed (pull-up, so LOW = pressed)
        applyLevel(BTN_LEVELS[i]);
        if (!publishLevel(currentLevel)) pendingPublish = true;
      }
    }
  }
}

void pollDesired() {
  if (Firebase.RTDB.getInt(&fbdo, "/fan/desired")) {
    int d = normalizeLevel(fbdo.intData());
    if (d != currentLevel) {
      applyLevel(d);
      publishLevel(d);
    }
  } else {
    Serial.printf("read failed: %s\n", fbdo.errorReason().c_str());
  }
}

// Scans for networks, then connects. Restarts the board if it can't connect in 20 s.
void connectWiFi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(200);

  int n = WiFi.scanNetworks();
  Serial.printf("Found %d networks:\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("  %s (%d dBm, channel %d)\n",
                  WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
  }

  // Diagnostics: show exactly what name/password length the firmware uses,
  // and the reason code whenever the connection drops.
  Serial.printf("Trying SSID '%s' (%d chars), password is %d chars\n",
                WIFI_SSID, (int)strlen(WIFI_SSID), (int)strlen(WIFI_PASSWORD));

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    Serial.printf("\nWi-Fi disconnect reason: %d\n", info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.printf(" [status %d]", WiFi.status());
    delay(500);
    if (millis() - t0 > 20000) {
      Serial.println("\nWi-Fi failed, restarting");
      ESP.restart();
    }
  }
  Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());
}

void relaySelfTest() {
  Serial.println("Relay self-test: 33, 66, 100, off");
  const int steps[4] = {33, 66, 100, 0};
  for (int i = 0; i < 4; i++) {
    applyLevel(steps[i]);
    delay(1500);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  for (int i = 0; i < 4; i++) pinMode(BTN_PINS[i], INPUT_PULLUP);
  for (int i = 0; i < 3; i++) setRelay(i, false);

#if RELAY_SELF_TEST
  relaySelfTest();
#endif

  connectWiFi();

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;
  Firebase.reconnectWiFi(true);
  Firebase.begin(&config, &auth);
}

void loop() {
#if DEBUG_BUTTONS
  // Prints only when a button changes state, in plain words.
  static int dbgLast[4] = {-1, -1, -1, -1};
  static const char* dbgName[4] = {"OFF    (GPIO19)", "LOW    (GPIO18)", "NORMAL (GPIO5) ", "HIGH   (GPIO21)"};
  for (int i = 0; i < 4; i++) {
    int v = digitalRead(BTN_PINS[i]);
    if (v != dbgLast[i]) {
      dbgLast[i] = v;
      Serial.printf("Button %s : %s\n", dbgName[i],
                    v == LOW ? "PRESSED  (pin connected to GND)" : "released");
    }
  }
#endif

  handleButtons();

  if (!Firebase.ready()) return;

  if (!firstSync) {                 // a press made while connecting is kept, not reset
    if (publishLevel(currentLevel)) {
      firstSync = true;
      Serial.println("Firebase ready. Waiting for buttons or web commands.");
    }
    return;
  }

  if (millis() - lastPoll > 1000) {
    lastPoll = millis();
    if (pendingPublish) {
      if (publishLevel(currentLevel)) pendingPublish = false;
    } else {
      pollDesired();
    }
  }
}