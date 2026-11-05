// Voice-controlled smart fan: ESP32 + Firebase Realtime Database
// Database layout:  /fan/desired  (set by web page / voice)
//                   /fan/reported (set by this device)
#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include "secrets.h"

// ---- Pins (same as the book) ----
const uint8_t BTN_PINS[4]     = {19, 18, 5, 17};    // OFF, 33%, 66%, 100%
const int     BTN_LEVELS[4]   = {0, 33, 66, 100};
const uint8_t RELAY_PINS[3]   = {32, 33, 25};       // low, normal, high
const int     RELAY_LEVELS[3] = {33, 66, 100};
const uint8_t PIN_OE          = 27;                 // logic converter enable

// If your relay module switches ON with a LOW signal, swap these two.
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

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

void applyLevel(int level) {
  for (int i = 0; i < 3; i++) digitalWrite(RELAY_PINS[i], RELAY_OFF);
  delay(50);  // break before make, so two relays are never on together
  for (int i = 0; i < 3; i++) {
    if (RELAY_LEVELS[i] == level) digitalWrite(RELAY_PINS[i], RELAY_ON);
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
    if (state != lastBtn[i] && millis() - lastChange[i] > 50) {
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

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) pinMode(BTN_PINS[i], INPUT_PULLUP);
  for (int i = 0; i < 3; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], RELAY_OFF);
  }
  pinMode(PIN_OE, OUTPUT);
  digitalWrite(PIN_OE, HIGH);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;
  Firebase.reconnectWiFi(true);
  Firebase.begin(&config, &auth);
}

void loop() {
  handleButtons();

  if (!Firebase.ready()) return;

  if (!firstSync) {                 // start safe: fan off, cloud matches
    applyLevel(0);
    if (publishLevel(0)) firstSync = true;
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