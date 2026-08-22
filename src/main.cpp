#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "controller_client.h"

#ifndef UEC_CONTROLLER_URL
#define UEC_CONTROLLER_URL "https://esp32-universal-controller.onrender.com"
#endif
#ifndef UEC_FIRMWARE_VERSION
#define UEC_FIRMWARE_VERSION "0.1.0"
#endif
#ifndef UEC_BUILD_ID
#define UEC_BUILD_ID "led-blink-central-test::0.1.0"
#endif
#ifndef UEC_BLINK_INTERVAL_MS
#define UEC_BLINK_INTERVAL_MS 1000
#endif

constexpr uint8_t LED_PIN = 2;
constexpr unsigned long BLINK_INTERVAL_MS = UEC_BLINK_INTERVAL_MS;
constexpr char APP_STATE_NAMESPACE[] = "uc_app_state";

ControllerClient controller(UEC_CONTROLLER_URL, UEC_FIRMWARE_VERSION, UEC_BUILD_ID);
Preferences appStatePreferences;
bool ledState = false;
bool deviceEnabled = true;
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
    ledState = false;
    digitalWrite(LED_PIN, LOW);
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
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  loadDeviceEnabledState();

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32 LED Blink - Central Test Project");
  Serial.println("========================================");
  Serial.printf("[FW] Version: %s | Build: %s | Hardware: esp32\n", UEC_FIRMWARE_VERSION, UEC_BUILD_ID);
  Serial.printf("[APP] LED GPIO: %u | Blink interval: %lu ms\n", LED_PIN, BLINK_INTERVAL_MS);
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
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    Serial.printf("[LED] GPIO %u -> %s\n", LED_PIN, ledState ? "ON" : "OFF");
  }

  delay(1);
}
