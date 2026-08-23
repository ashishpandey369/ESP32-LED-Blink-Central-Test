#include <Arduino.h>
#include "controller_client.h"
#include "version.h"

// Example main demonstrating simple integration. Drop this into your project's
// src/ folder or copy only controller_client.* and version.h into your project
// and adapt as needed.

ControllerClient controller(UEC_CONTROLLER_URL, UEC_FIRMWARE_VERSION, UEC_BUILD_ID);

const int LED1 = 2; // D2
const int LED2 = 4; // D4

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);

  controller.begin();
}

void loop() {
  controller.loop();

  // Alternate LEDs as an example (D2 on -> D4 off, then swap)
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, LOW);
  delay(UEC_BLINK_INTERVAL_MS);
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, HIGH);
  delay(UEC_BLINK_INTERVAL_MS);

  // Optionally consume remote messages
  String msg;
  if (controller.consumeMessage(msg)) {
    Serial.printf("[APP] Remote message received: %s\n", msg.c_str());
  }
}
