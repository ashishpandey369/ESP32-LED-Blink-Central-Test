#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "controller_client.h"

#ifndef UEC_CONTROLLER_URL
#define UEC_CONTROLLER_URL "https://esp32-universal-controller.onrender.com"
#endif
#ifndef UEC_FIRMWARE_VERSION
#define UEC_FIRMWARE_VERSION "0.5.2"
#endif
#ifndef UEC_BUILD_ID
#define UEC_BUILD_ID "led-blink-central-test::0.5.2"
#endif
#ifndef UEC_BLINK_INTERVAL_MS
#define UEC_BLINK_INTERVAL_MS 1000
#endif

constexpr uint8_t LED_D2_PIN = 2;
constexpr uint8_t LED_D4_PIN = 4;
constexpr unsigned long BLINK_INTERVAL_MS = UEC_BLINK_INTERVAL_MS;
constexpr char APP_STATE_NAMESPACE[] = "uc_app_state";

ControllerClient controller(UEC_CONTROLLER_URL, UEC_FIRMWARE_VERSION, UEC_BUILD_ID);
Preferences appStatePreferences;
bool deviceEnabled = true;
bool d2Active = true;
unsigned long lastBlinkAt = 0;

void loadDeviceEnabledState() {
  appStatePreferences.begin(APP_STATE_NAMESPACE, true);
  deviceEnabled = appStatePreferences.getBool("enabled", true);
  appStatePreferences.end();
  Serial.printf("[DEVICE] Application state: %s\n", deviceEnabled ? "ENABLED" : "DISABLED");
}

void setDeviceEnabled(bool enabled) {
  deviceEnabled = enabled;
  appStatePreferences.begin(APP_STATE_NAMESPACE, false);
  appStatePreferences.putBool("enabled", deviceEnabled);
  appStatePreferences.end();
  lastBlinkAt = millis();

  if (!deviceEnabled) {
    d2Active = true;
    digitalWrite(LED_D2_PIN, LOW);
    digitalWrite(LED_D4_PIN, LOW);
  }

  Serial.printf("[DEVICE] Remote application control -> %s\n", deviceEnabled ? "ENABLED" : "DISABLED");
}

void controllerTask(void*) {
  for (;;) {
    controller.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_D2_PIN, OUTPUT);
  pinMode(LED_D4_PIN, OUTPUT);
  digitalWrite(LED_D2_PIN, LOW);
  digitalWrite(LED_D4_PIN, LOW);
  loadDeviceEnabledState();

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32 LED Blink - Central Test Project");
  Serial.println("========================================");
  Serial.printf("[FW] Version: %s | Build: %s | Hardware: esp32\n", UEC_FIRMWARE_VERSION, UEC_BUILD_ID);
  Serial.printf("[APP] LED pins: D2=%u, D4=%u | Alternating interval: %lu ms\n", LED_D2_PIN, LED_D4_PIN, BLINK_INTERVAL_MS);
  controller.begin();

  xTaskCreatePinnedToCore(
    controllerTask,
    "controller_task",
    8192,
    nullptr,
    1,
    nullptr,
    0
  );

  Serial.println("[CONTROLLER] Background controller task started on core 0.");
  Serial.println("[APP] Main application loop remains free for device work.");
}

void loop() {
  String remoteMessage;
  if (controller.consumeMessage(remoteMessage)) {
    if (remoteMessage == "__UC_DEVICE_ENABLE__") {
      setDeviceEnabled(true);
    } else if (remoteMessage == "__UC_DEVICE_DISABLE__") {
      setDeviceEnabled(false);
    } else {
      Serial.println("----------------------------------------");
      Serial.println("REMOTE MESSAGE FROM CONTROLLER:");
      Serial.println(remoteMessage);
      Serial.println("----------------------------------------");
    }
  }

  if (!deviceEnabled) {
    delay(1);
    return;
  }

  const unsigned long now = millis();
  if (now - lastBlinkAt >= BLINK_INTERVAL_MS) {
    lastBlinkAt = now;
    d2Active = !d2Active;
    digitalWrite(LED_D2_PIN, d2Active ? HIGH : LOW);
    digitalWrite(LED_D4_PIN, d2Active ? LOW : HIGH);
    Serial.printf("[LED] D2 -> %s | D4 -> %s\n", d2Active ? "ON" : "OFF", d2Active ? "OFF" : "ON");
  }

  delay(1);
}
