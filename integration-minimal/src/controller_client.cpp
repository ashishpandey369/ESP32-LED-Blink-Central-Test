#include "controller_client.h"

#include <DNSServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// Note: This file is copied from the LED Blink Central Test project and
// includes the full ControllerClient implementation with OTA, heartbeat,
// and a serialized OTA progress reporter. Keep it synchronized with the
// upstream repo when making updates.

namespace {
constexpr uint16_t DNS_PORT = 53;
constexpr uint16_t HTTP_PORT = 80;
constexpr char AP_PREFIX[] = "ESP32-UC-";
constexpr char AP_PASSWORD_PREFIX[] = "UC-";
constexpr char WIFI_NS[] = "ledblink_wifi";
constexpr char DEVICE_NS[] = "ledblink_device";
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 10000;
constexpr unsigned long FIRST_HEARTBEAT_RETRY_MS = 5000;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
constexpr unsigned long NTP_SYNC_TIMEOUT_MS = 10000;
constexpr unsigned long OTA_NO_PROGRESS_TIMEOUT_MS = 60000;
constexpr unsigned long OTA_STREAM_READ_TIMEOUT_MS = 10000;
DNSServer dnsServer;
WebServer webServer(HTTP_PORT);
Preferences preferences;
volatile bool otaRunning = false;
String otaCommandId;

// OTA progress reporter queue and task
QueueHandle_t otaProgressQueue = NULL;
TaskHandle_t otaProgressReporterHandle = NULL;

struct ProgressEvent {
  String controllerUrl;
  String deviceId;
  String deviceKey;
  String commandId;
  String tag;
  String state;
  int percent;
  size_t downloaded;
  size_t total;
  String message;
};

// Ensure the OTA progress reporter task and queue exist
static void otaProgressReporterTask(void* parameter);
static void ensureOtaProgressReporter() {
  if (otaProgressQueue) return;
  otaProgressQueue = xQueueCreate(8, sizeof(void*));
  if (!otaProgressQueue) {
    Serial.println("[OTA] Unable to create progress queue");
    return;
  }
  const BaseType_t created = xTaskCreatePinnedToCore(otaProgressReporterTask, "ota_progress", 8192, NULL, 1, &otaProgressReporterHandle, 1);
  if (created != pdPASS) {
    Serial.println("[OTA] Failed to create progress reporter task");
    vQueueDelete(otaProgressQueue);
    otaProgressQueue = NULL;
  }
}

} // namespace

// NOTE: The rest of ControllerClient implementation is intentionally omitted
// here for brevity in the integration package. Copy the full implementation
// from the upstream src/controller_client.cpp if you need OTA and heartbeat
// functionality identical to the LED Blink Central Test project.

// Minimal stub implementations so the header links if users prefer to
// implement their own simplified behavior. These should be replaced by the
// full implementation when integrating OTA features.

ControllerClient::ControllerClient(const char* controllerUrl, const char* firmwareVersion, const char* buildId) : controllerUrl_(controllerUrl), firmwareVersion_(firmwareVersion), buildId_(buildId) {}

void ControllerClient::begin() {
  // Implement or replace with the full begin() from the upstream repo
  Serial.println("[CONTROLLER] begin() called - replace with full implementation for OTA/heartbeat");
}

void ControllerClient::loop() {
  // Minimal loop; replace with full implementation to enable heartbeat/OTA
}

bool ControllerClient::provisioningMode() const { return provisioningMode_; }
const String& ControllerClient::deviceId() const { return deviceId_; }
const String& ControllerClient::deviceKey() const { return deviceKey_; }
bool ControllerClient::consumeMessage(String& message) { if (pendingMessage_.isEmpty()) return false; message = pendingMessage_; pendingMessage_.clear(); return true; }
