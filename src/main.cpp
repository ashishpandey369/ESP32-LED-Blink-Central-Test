#include <Arduino.h>
#include "controller_client.h"

#ifndef UEC_CONTROLLER_URL
#define UEC_CONTROLLER_URL "https://esp32-universal-controller.onrender.com"
#endif

#ifndef UEC_FIRMWARE_VERSION
#define UEC_FIRMWARE_VERSION "0.1.0"
#endif

#ifndef UEC_BUILD_ID
#define UEC_BUILD_ID "led-blink-test-v0.1.0"
#endif

// Application code stays separate from device-management code.
// This is the pattern future ESP32 projects can reuse.
constexpr uint8_t LED_PIN = LED_BUILTIN;
constexpr unsigned long BLINK_INTERVAL_MS = 1000;

ControllerClient controller(UEC_CONTROLLER_URL, UEC_FIRMWARE_VERSION, UEC_BUILD_ID);

bool ledState = false;
unsigned long lastBlinkAt = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32 LED Blink - Central Test Project");
  Serial.println("========================================");

  controller.begin();
}

void loop() {
  controller.loop();

  const unsigned long now = millis();
  if (now - lastBlinkAt >= BLINK_INTERVAL_MS) {
    lastBlinkAt = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    Serial.printf("[LED] %s\n", ledState ? "ON" : "OFF");
  }

  String remoteMessage;
  if (controller.consumeMessage(remoteMessage)) {
    Serial.println("----------------------------------------");
    Serial.println("REMOTE MESSAGE FROM CONTROLLER:");
    Serial.println(remoteMessage);
    Serial.println("----------------------------------------");
  }
}
