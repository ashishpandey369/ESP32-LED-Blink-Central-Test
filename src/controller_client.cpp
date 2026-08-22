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

bool syncClock() {
  Serial.println("[TIME] Synchronizing clock...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  const unsigned long deadline = millis() + NTP_SYNC_TIMEOUT_MS;
  time_t now = 0;
  while (millis() < deadline) {
    time(&now);
    if (now > 1700000000) {
      struct tm timeInfo;
      if (getLocalTime(&timeInfo, 1000)) Serial.printf("[TIME] Synchronized: %04d-%02d-%02d %02d:%02d:%02d UTC\n", timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday, timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
      return true;
    }
    delay(250);
  }
  Serial.println("[TIME] NTP synchronization timed out.");
  return false;
}

bool resolveControllerHost() {
  IPAddress resolved;
  constexpr char HOST[] = "esp32-universal-controller.onrender.com";
  Serial.printf("[NET] Resolving %s...\n", HOST);
  if (WiFi.hostByName(HOST, resolved) == 1) {
    Serial.printf("[NET] DNS OK: %s -> %s\n", HOST, resolved.toString().c_str());
    return true;
  }
  Serial.println("[NET] DNS FAILED.");
  return false;
}

String htmlEscape(const String& value) {
  String out;
  out.reserve(value.length() + 16);
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += value[i]; break;
    }
  }
  return out;
}

String jsonValue(const String& json, const String& key, size_t from = 0) {
  const String marker = String("\"") + key + "\":\"";
  const int start = json.indexOf(marker, from);
  if (start < 0) return "";
  const int valueStart = start + marker.length();
  int end = valueStart;
  while (end < static_cast<int>(json.length())) {
    if (json[end] == '"' && (end == valueStart || json[end - 1] != '\\')) break;
    ++end;
  }
  String value = json.substring(valueStart, end);
  value.replace("\\\"", "\"");
  value.replace("\\n", "\n");
  value.replace("\\r", "\r");
  value.replace("\\\\", "\\");
  return value;
}

struct OtaTaskContext {
  String controllerUrl;
  String deviceId;
  String deviceKey;
  String commandId;
  String tag;
  String version;
};

void reportOtaProgress(const OtaTaskContext& ctx, const char* state, int percent, size_t downloaded, size_t total, const String& detail) {
  if (WiFi.status() != WL_CONNECTED || ctx.controllerUrl.isEmpty() || ctx.commandId.isEmpty()) return;
  WiFiClientSecure progressClient;
  progressClient.setInsecure();
  HTTPClient progressHttp;
  const String progressUrl = ctx.controllerUrl + "/api/device/ota-progress";
  if (!progressHttp.begin(progressClient, progressUrl)) return;
  progressHttp.setConnectTimeout(5000);
  progressHttp.setTimeout(5000);
  progressHttp.addHeader("Content-Type", "application/json");
  progressHttp.addHeader("X-Device-Key", ctx.deviceKey);
  String body = "{\"deviceId\":\"" + ctx.deviceId + "\",\"commandId\":\"" + ctx.commandId + "\",\"status\":\"" + String(state) + "\",\"percent\":" + String(percent) + ",\"downloaded\":" + String(downloaded) + ",\"total\":" + String(total) + ",\"tag\":\"" + ctx.tag + "\",\"message\":\"" + detail + "\"}";
  const int status = progressHttp.POST(body);
  Serial.printf("[OTA] Progress report: %s %d%% HTTP %d\n", state, percent, status);
  progressHttp.end();
}

void otaTask(void* parameter) {
  OtaTaskContext* ctx = static_cast<OtaTaskContext*>(parameter);
  Serial.printf("[OTA] Background task started: command=%s version=%s\n", ctx->commandId.c_str(), ctx->version.c_str());

  const String url = ctx->controllerUrl + "/api/device/firmware/download/" + ctx->tag + "?deviceId=" + ctx->deviceId;
  Serial.printf("[OTA] Downloading: %s\n", url.c_str());
  reportOtaProgress(*ctx, "downloading", 0, 0, 0, "Firmware download started");

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(OTA_STREAM_READ_TIMEOUT_MS);
  if (!http.begin(secureClient, url)) {
    reportOtaProgress(*ctx, "failed", 0, 0, 0, "HTTP initialization failed");
    otaRunning = false;
    delete ctx;
    vTaskDelete(nullptr);
    return;
  }
  http.addHeader("X-Device-Key", ctx->deviceKey);
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("[OTA] Download failed: HTTP %d | %s\n", status, http.getString().c_str());
    reportOtaProgress(*ctx, "failed", 0, 0, 0, String("Firmware download HTTP ") + String(status));
    http.end();
    otaRunning = false;
    delete ctx;
    vTaskDelete(nullptr);
    return;
  }

  const int contentLength = http.getSize();
  Serial.printf("[OTA] HTTP 200 | firmware size=%d bytes\n", contentLength);
  if (contentLength <= 0 || !Update.begin(static_cast<size_t>(contentLength))) {
    Serial.println("[OTA] Invalid firmware size or Update.begin failed.");
    reportOtaProgress(*ctx, "failed", 0, 0, contentLength > 0 ? static_cast<size_t>(contentLength) : 0, "Invalid firmware size or insufficient OTA space");
    http.end();
    otaRunning = false;
    delete ctx;
    vTaskDelete(nullptr);
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  const size_t total = static_cast<size_t>(contentLength);
  size_t downloaded = 0;
  int lastBucket = -1;
  unsigned long lastProgressAt = millis();
  unsigned long lastReportAt = 0;
  unsigned long lastWaitLogAt = 0;
  uint8_t buffer[2048];
  bool writeOk = true;

  while (downloaded < total) {
    const size_t remaining = total - downloaded;
    const size_t want = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    const size_t count = stream->readBytes(buffer, want);
    if (count == 0) {
      const unsigned long now = millis();
      const unsigned long idle = now - lastProgressAt;
      if (idle >= OTA_NO_PROGRESS_TIMEOUT_MS) {
        Serial.println("[OTA] No download progress for 60 seconds. Aborting.");
        writeOk = false;
        break;
      }
      if (lastWaitLogAt == 0 || now - lastWaitLogAt >= 10000) {
        Serial.printf("[OTA] Waiting for stream... %lu seconds remaining.\n", (OTA_NO_PROGRESS_TIMEOUT_MS - idle) / 1000UL);
        lastWaitLogAt = now;
      }
      yield();
      continue;
    }

    const size_t written = Update.write(buffer, count);
    if (written != count) {
      Serial.printf("[OTA] Update.write mismatch | read=%u | written=%u | update_error=%d\n", static_cast<unsigned>(count), static_cast<unsigned>(written), static_cast<int>(Update.getError()));
      writeOk = false;
      break;
    }

    downloaded += count;
    lastProgressAt = millis();
    lastWaitLogAt = 0;
    const int percent = static_cast<int>((downloaded * 100ULL) / total);
    const int bucket = percent / 10;
    const unsigned long now = millis();
    if (bucket != lastBucket || now - lastReportAt >= 750 || downloaded == total) {
      reportOtaProgress(*ctx, "downloading", percent, downloaded, total, "Downloading firmware");
      Serial.printf("[OTA] Download progress: %d%% | %u/%u bytes\n", percent, static_cast<unsigned>(downloaded), static_cast<unsigned>(total));
      lastBucket = bucket;
      lastReportAt = now;
    }
    yield();
  }

  const bool valid = writeOk && downloaded == total && Update.end(true);
  http.end();
  if (!valid) {
    Update.abort();
    const int percent = total ? static_cast<int>((downloaded * 100ULL) / total) : 0;
    const String detail = downloaded < total ? "Firmware download stalled for 60 seconds" : "Firmware image write failed";
    reportOtaProgress(*ctx, "failed", percent, downloaded, total, detail);
    otaRunning = false;
    delete ctx;
    vTaskDelete(nullptr);
    return;
  }

  reportOtaProgress(*ctx, "installing", 100, total, total, "Firmware written successfully; installing");
  delay(250);
  reportOtaProgress(*ctx, "rebooting", 100, total, total, "Restarting into the new firmware");
  Serial.println("[OTA] Update complete. Restarting...");
  delay(250);
  ESP.restart();
}
}

ControllerClient::ControllerClient(const char* controllerUrl, const char* firmwareVersion, const char* buildId) : controllerUrl_(controllerUrl), firmwareVersion_(firmwareVersion), buildId_(buildId) {}
String ControllerClient::chipIdHex() const { const uint64_t chipId = ESP.getEfuseMac(); char buffer[13]; snprintf(buffer, sizeof(buffer), "%012llX", chipId); return String(buffer); }
String ControllerClient::makeDeviceId() const { return String("esp32-") + chipIdHex(); }
String ControllerClient::generateDeviceKey() const { static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"; String key; key.reserve(48); for (int i = 0; i < 48; ++i) key += alphabet[esp_random() % (sizeof(alphabet) - 1)]; return key; }
void ControllerClient::loadOrCreateDeviceKey() { preferences.begin(DEVICE_NS, false); deviceKey_ = preferences.getString("device_key", ""); if (deviceKey_.length() < 32) { deviceKey_ = generateDeviceKey(); preferences.putString("device_key", deviceKey_); Serial.println("[DEVICE] Generated permanent device key."); } else Serial.println("[DEVICE] Loaded existing permanent device key."); preferences.end(); }
bool ControllerClient::connectSavedWiFi() { preferences.begin(WIFI_NS, true); const String ssid = preferences.getString("ssid", ""); const String password = preferences.getString("password", ""); preferences.end(); if (ssid.isEmpty()) return false; WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); const IPAddress dns1(1, 1, 1, 1); const IPAddress dns2(8, 8, 8, 8); WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), dns1, dns2); WiFi.begin(ssid.c_str(), password.c_str()); Serial.printf("[WIFI] Connecting to %s", ssid.c_str()); const unsigned long deadline = millis() + 15000; while (WiFi.status() != WL_CONNECTED && millis() < deadline) { delay(250); Serial.print('.'); } Serial.println(); if (WiFi.status() != WL_CONNECTED) { Serial.printf("[WIFI] Initial connection failed. Status=%d\n", WiFi.status()); return false; } Serial.printf("[WIFI] Connected. IP: %s\n", WiFi.localIP().toString().c_str()); Serial.printf("[NET] DNS1: %s | DNS2: %s\n", WiFi.dnsIP(0).toString().c_str(), WiFi.dnsIP(1).toString().c_str()); provisioningMode_ = false; syncClock(); resolveControllerHost(); return true; }
void ControllerClient::startProvisioning() { provisioningMode_ = true; WiFi.mode(WIFI_AP_STA); const String apSsid = String(AP_PREFIX) + chipIdHex().substring(6); const String apPassword = String(AP_PASSWORD_PREFIX) + chipIdHex().substring(6); WiFi.softAP(apSsid.c_str(), apPassword.c_str()); const IPAddress apIp = WiFi.softAPIP(); dnsServer.start(DNS_PORT, "*", apIp); WiFi.scanDelete(); WiFi.scanNetworks(true, true); Serial.println("\n========== DEVICE PROVISIONING =========="); Serial.printf("Device ID : %s\n", deviceId_.c_str()); Serial.printf("Device key: %s\n", deviceKey_.c_str()); Serial.printf("Firmware  : %s\n", firmwareVersion_.c_str()); Serial.printf("Build ID  : %s\n", buildId_.c_str()); Serial.printf("Setup AP  : %s\n", apSsid.c_str()); Serial.printf("AP pass   : %s\n", apPassword.c_str()); Serial.printf("Setup URL : http://%s/\n", apIp.toString().c_str()); Serial.println("========================================="); }
void ControllerClient::startWebServer() { webServer.on("/", HTTP_GET, [this]() { handleProvisioningRequests(); }); webServer.on("/info", HTTP_GET, [this]() { String body = "{\"deviceId\":\"" + deviceId_ + "\",\"firmwareVersion\":\"" + firmwareVersion_ + "\",\"buildId\":\"" + buildId_ + "\"}"; webServer.send(200, "application/json", body); }); webServer.on("/save", HTTP_POST, [this]() { if (!provisioningMode_) { webServer.send(403, "text/plain", "Provisioning mode is not active."); return; } const String ssid = webServer.arg("ssid"), password = webServer.arg("password"); if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) { webServer.send(400, "text/plain", "Invalid Wi-Fi details."); return; } preferences.begin(WIFI_NS, false); preferences.putString("ssid", ssid); preferences.putString("password", password); preferences.end(); webServer.send(200, "text/html", "<h2>Saved.</h2><p>The ESP32 is restarting and will connect to Wi-Fi.</p>"); delay(500); ESP.restart(); }); webServer.on("/scan", HTTP_GET, [this]() { WiFi.scanDelete(); WiFi.scanNetworks(true, true); webServer.sendHeader("Location", "/"); webServer.send(303); }); webServer.onNotFound([this]() { if (provisioningMode_) { webServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/"); webServer.send(302, "text/plain", "Redirecting to setup"); } else webServer.send(404, "text/plain", "Not found"); }); webServer.begin(); }
void ControllerClient::handleProvisioningRequests() { if (!provisioningMode_) { webServer.send(200, "application/json", "{\"ok\":true}"); return; } const int count = WiFi.scanComplete(); String options = "<option value=\"\">Select a network</option>"; if (count > 0) for (int i = 0; i < count; ++i) { const String ssid = WiFi.SSID(i); if (!ssid.isEmpty()) options += "<option value=\"" + htmlEscape(ssid) + "\">" + htmlEscape(ssid) + "</option>"; } const String html = String("<!doctype html><html><meta name='viewport' content='width=device-width,initial-scale=1'><title>ESP32 LED Blink</title><body style='font-family:system-ui;max-width:520px;margin:40px auto;padding:20px'><h1>ESP32 LED Blink</h1><p>Device: <b>") + htmlEscape(deviceId_) + "</b></p><p>Firmware: <b>" + htmlEscape(firmwareVersion_) + "</b></p><p>Device key is available in Serial Monitor.</p><form method='post' action='/save'><label>Wi-Fi</label><br><select name='ssid' style='width:100%;padding:12px'>" + options + "</select><br><br><label>Password</label><br><input name='password' type='password' style='width:100%;padding:12px'><br><button style='margin-top:16px;padding:12px;width:100%'>Connect</button></form><form method='get' action='/scan'><button style='margin-top:10px;padding:12px;width:100%'>Scan again</button></form></body></html>"; webServer.send(200, "text/html", html); }
void ControllerClient::begin() { deviceId_ = makeDeviceId(); loadOrCreateDeviceKey(); Serial.printf("[DEVICE] Device key: %s\n", deviceKey_.c_str()); Serial.printf("[CONTROLLER] URL: %s\n", controllerUrl_.c_str()); Serial.printf("[CONTROLLER] Device ID: %s\n", deviceId_.c_str()); Serial.printf("[CONTROLLER] Firmware: %s | Build: %s\n", firmwareVersion_.c_str(), buildId_.c_str()); if (!connectSavedWiFi()) startProvisioning(); startWebServer(); if (!provisioningMode_) { Serial.println("[CONTROLLER] Wi-Fi ready. Sending first heartbeat now..."); sendHeartbeat(); } }
void ControllerClient::queueAck(const String& id, const char* status, const String& result) { if (id.isEmpty()) return; if (pendingAcks_.length()) pendingAcks_ += ','; pendingAcks_ += "{\"id\":\"" + id + "\",\"status\":\"" + String(status) + "\",\"result\":\"" + result + "\"}"; }

void ControllerClient::processCommands(const String& json) {
  const int commandsStart = json.indexOf("\"commands\":["); if (commandsStart < 0) return; int cursor = commandsStart;
  while (true) {
    const int idPos = json.indexOf("\"id\":\"", cursor); if (idPos < 0) break; const String id = jsonValue(json, "id", idPos); const int typePos = json.indexOf("\"type\":\"", idPos); if (typePos < 0) break; const String type = jsonValue(json, "type", typePos); const int nextCommand = json.indexOf("{\"id\":\"", typePos + 8); const int commandEnd = nextCommand >= 0 ? nextCommand : json.length(); const String commandJson = json.substring(idPos, commandEnd);
    Serial.printf("[COMMAND] Received id=%s type=%s\n", id.c_str(), type.c_str());
    if (type == "message") { const int payloadPos = commandJson.indexOf("\"payload\":{"); const String msg = payloadPos >= 0 ? jsonValue(commandJson, "message", payloadPos) : jsonValue(commandJson, "message", typePos - idPos); if (!msg.isEmpty()) { pendingMessage_ = msg; Serial.printf("[COMMAND] Remote message: %s\n", pendingMessage_.c_str()); queueAck(id, "executed", "message_received"); } else queueAck(id, "failed", "missing_message"); }
    else if (type == "ota") {
      const int payloadPos = commandJson.indexOf("\"payload\":{");
      const String tag = payloadPos >= 0 ? jsonValue(commandJson, "tag", payloadPos) : jsonValue(commandJson, "tag");
      const String version = payloadPos >= 0 ? jsonValue(commandJson, "version", payloadPos) : jsonValue(commandJson, "version");
      Serial.printf("[COMMAND] OTA requested: tag=%s version=%s\n", tag.c_str(), version.c_str());
      if (tag.isEmpty()) { Serial.println("[OTA] Command missing payload.tag"); queueAck(id, "failed", "missing_tag"); }
      else if (version == firmwareVersion_) { Serial.println("[OTA] Already running requested version."); queueAck(id, "failed", "already_running_requested_version"); }
      else if (otaRunning) { Serial.printf("[OTA] OTA already running (command=%s); ignoring duplicate command=%s\n", otaCommandId.c_str(), id.c_str()); }
      else {
        OtaTaskContext* context = new OtaTaskContext{controllerUrl_, deviceId_, deviceKey_, id, tag, version};
        if (!context) { queueAck(id, "failed", "unable_to_allocate_ota_context"); }
        else {
          otaCommandId = id;
          otaRunning = true;
          const BaseType_t created = xTaskCreatePinnedToCore(otaTask, "ota_update", 12288, context, 1, nullptr, 0);
          if (created != pdPASS) { otaRunning = false; otaCommandId.clear(); delete context; queueAck(id, "failed", "unable_to_start_ota_task"); Serial.println("[OTA] Failed to create background OTA task."); }
          else Serial.printf("[OTA] Background OTA task created for command=%s\n", id.c_str());
        }
      }
    }
    else queueAck(id, "rejected", "unsupported_command");
    if (nextCommand < 0) break; cursor = nextCommand;
  }
}

bool ControllerClient::performOta(const String&, const String&, const String&) {
  Serial.println("[OTA] Legacy synchronous OTA path is disabled; use background OTA task.");
  return false;
}

void ControllerClient::sendHeartbeat() {
  if (provisioningMode_ || WiFi.status() != WL_CONNECTED) return; if (lastHeartbeatAt_ != 0 && millis() - lastHeartbeatAt_ < HEARTBEAT_INTERVAL_MS) return; lastHeartbeatAt_ = millis();
  if (!resolveControllerHost()) { Serial.println("[HEARTBEAT] DNS unavailable; retrying soon."); lastHeartbeatAt_ = millis() - (HEARTBEAT_INTERVAL_MS - FIRST_HEARTBEAT_RETRY_MS); return; }
  WiFiClientSecure secureClient; secureClient.setInsecure(); HTTPClient http; const String url = controllerUrl_ + "/api/device/heartbeat";
  if (!http.begin(secureClient, url)) { Serial.println("[HEARTBEAT] HTTPS begin failed; retrying soon."); lastHeartbeatAt_ = millis() - (HEARTBEAT_INTERVAL_MS - FIRST_HEARTBEAT_RETRY_MS); return; }
  http.setConnectTimeout(10000); http.setTimeout(10000); http.addHeader("Content-Type", "application/json"); http.addHeader("X-Device-Key", deviceKey_); http.addHeader("User-Agent", "ESP32-LED-Blink-Central-Test/" + firmwareVersion_);
  String body = "{\"deviceId\":\"" + deviceId_ + "\",\"firmwareVersion\":\"" + firmwareVersion_ + "\",\"buildId\":\"" + buildId_ + "\",\"hardware\":\"esp32\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"uptime\":" + String(millis()); if (pendingAcks_.length()) { body += ",\"commandAcks\":[" + pendingAcks_ + "]"; pendingAcks_.clear(); } body += "}";
  Serial.printf("[HEARTBEAT] POST %s\n", url.c_str()); const int status = http.POST(body); const String response = http.getString(); if (status == HTTP_CODE_OK) { Serial.printf("[HEARTBEAT] HTTP 200 | %s\n", response.c_str()); processCommands(response); } else { Serial.printf("[HEARTBEAT] FAILED HTTP %d | %s\n", status, response.c_str()); lastHeartbeatAt_ = millis() - (HEARTBEAT_INTERVAL_MS - FIRST_HEARTBEAT_RETRY_MS); } http.end();
}
void ControllerClient::loop() { if (provisioningMode_) dnsServer.processNextRequest(); webServer.handleClient(); if (WiFi.status() == WL_CONNECTED) sendHeartbeat(); else if (!provisioningMode_ && millis() - lastWiFiRetryAt_ >= WIFI_RETRY_INTERVAL_MS) { lastWiFiRetryAt_ = millis(); Serial.println("[WIFI] Disconnected. Retrying saved Wi-Fi..."); WiFi.reconnect(); } }
bool ControllerClient::provisioningMode() const { return provisioningMode_; }
const String& ControllerClient::deviceId() const { return deviceId_; }
const String& ControllerClient::deviceKey() const { return deviceKey_; }
bool ControllerClient::consumeMessage(String& message) { if (pendingMessage_.isEmpty()) return false; message = pendingMessage_; pendingMessage_.clear(); return true; }
