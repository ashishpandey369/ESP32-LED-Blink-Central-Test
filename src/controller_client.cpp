#include "controller_client.h"

#include <DNSServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

namespace {
constexpr uint16_t DNS_PORT = 53;
constexpr uint16_t HTTP_PORT = 80;
constexpr char AP_PREFIX[] = "ESP32-UC-";
constexpr char AP_PASSWORD_PREFIX[] = "UC-";
constexpr char WIFI_NS[] = "ledblink_wifi";
constexpr char DEVICE_NS[] = "ledblink_device";
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 30000;
constexpr unsigned long FIRST_HEARTBEAT_RETRY_MS = 5000;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;

DNSServer dnsServer;
WebServer webServer(HTTP_PORT);
Preferences preferences;

String htmlEscape(const String& value) { String out; out.reserve(value.length() + 16); for (size_t i = 0; i < value.length(); ++i) { switch (value[i]) { case '&': out += "&amp;"; break; case '<': out += "&lt;"; break; case '>': out += "&gt;"; break; case '"': out += "&quot;"; break; case '\'': out += "&#39;"; break; default: out += value[i]; break; } } return out; }
String jsonValue(const String& json, const String& key, size_t from = 0) { const String marker = String("\"") + key + "\":\""; const int start = json.indexOf(marker, from); if (start < 0) return ""; const int valueStart = start + marker.length(); int end = valueStart; while (end < static_cast<int>(json.length())) { if (json[end] == '"' && (end == valueStart || json[end - 1] != '\\')) break; ++end; } String value = json.substring(valueStart, end); value.replace("\\\"", "\""); value.replace("\\n", "\n"); value.replace("\\r", "\r"); value.replace("\\\\", "\\"); return value; }
}

ControllerClient::ControllerClient(const char* controllerUrl, const char* firmwareVersion, const char* buildId) : controllerUrl_(controllerUrl), firmwareVersion_(firmwareVersion), buildId_(buildId) {}
String ControllerClient::chipIdHex() const { const uint64_t chipId = ESP.getEfuseMac(); char buffer[13]; snprintf(buffer, sizeof(buffer), "%012llX", chipId); return String(buffer); }
String ControllerClient::makeDeviceId() const { return String("esp32-") + chipIdHex(); }
String ControllerClient::generateDeviceKey() const { static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"; String key; key.reserve(48); for (int i = 0; i < 48; ++i) key += alphabet[esp_random() % (sizeof(alphabet) - 1)]; return key; }

void ControllerClient::loadOrCreateDeviceKey() { preferences.begin(DEVICE_NS, false); deviceKey_ = preferences.getString("device_key", ""); if (deviceKey_.length() < 32) { deviceKey_ = generateDeviceKey(); preferences.putString("device_key", deviceKey_); Serial.println("[DEVICE] Generated permanent device key."); } else Serial.println("[DEVICE] Loaded existing permanent device key."); preferences.end(); }

bool ControllerClient::connectSavedWiFi() { preferences.begin(WIFI_NS, true); const String ssid = preferences.getString("ssid", ""); const String password = preferences.getString("password", ""); preferences.end(); if (ssid.isEmpty()) return false; WiFi.mode(WIFI_STA); WiFi.begin(ssid.c_str(), password.c_str()); Serial.printf("[WIFI] Connecting to %s", ssid.c_str()); const unsigned long deadline = millis() + 15000; while (WiFi.status() != WL_CONNECTED && millis() < deadline) { delay(250); Serial.print('.'); } Serial.println(); if (WiFi.status() != WL_CONNECTED) return false; Serial.printf("[WIFI] Connected. IP: %s\n", WiFi.localIP().toString().c_str()); provisioningMode_ = false; return true; }

void ControllerClient::startProvisioning() { provisioningMode_ = true; WiFi.mode(WIFI_AP_STA); const String apSsid = String(AP_PREFIX) + chipIdHex().substring(6); const String apPassword = String(AP_PASSWORD_PREFIX) + chipIdHex().substring(6); WiFi.softAP(apSsid.c_str(), apPassword.c_str()); const IPAddress apIp = WiFi.softAPIP(); dnsServer.start(DNS_PORT, "*", apIp); WiFi.scanDelete(); WiFi.scanNetworks(true, true); Serial.println("\n========== DEVICE PROVISIONING =========="); Serial.printf("Device ID : %s\n", deviceId_.c_str()); Serial.printf("Device key: %s\n", deviceKey_.c_str()); Serial.printf("Firmware  : %s\n", firmwareVersion_.c_str()); Serial.printf("Build ID  : %s\n", buildId_.c_str()); Serial.printf("Setup AP  : %s\n", apSsid.c_str()); Serial.printf("AP pass   : %s\n", apPassword.c_str()); Serial.printf("Setup URL : http://%s/\n", apIp.toString().c_str()); Serial.println("========================================="); }

void ControllerClient::startWebServer() { webServer.on("/", HTTP_GET, [this]() { handleProvisioningRequests(); }); webServer.on("/info", HTTP_GET, [this]() { String body = "{\"deviceId\":\"" + deviceId_ + "\",\"firmwareVersion\":\"" + firmwareVersion_ + "\",\"buildId\":\"" + buildId_ + "\"}"; webServer.send(200, "application/json", body); }); webServer.on("/save", HTTP_POST, [this]() { if (!provisioningMode_) { webServer.send(403, "text/plain", "Provisioning mode is not active."); return; } const String ssid = webServer.arg("ssid"), password = webServer.arg("password"); if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) { webServer.send(400, "text/plain", "Invalid Wi-Fi details."); return; } preferences.begin(WIFI_NS, false); preferences.putString("ssid", ssid); preferences.putString("password", password); preferences.end(); webServer.send(200, "text/html", "<h2>Saved.</h2><p>The ESP32 is restarting and will connect to Wi-Fi.</p>"); delay(500); ESP.restart(); }); webServer.on("/scan", HTTP_GET, [this]() { WiFi.scanDelete(); WiFi.scanNetworks(true, true); webServer.sendHeader("Location", "/"); webServer.send(303); }); webServer.onNotFound([this]() { if (provisioningMode_) { webServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/"); webServer.send(302, "text/plain", "Redirecting to setup"); } else webServer.send(404, "text/plain", "Not found"); }); webServer.begin(); }

void ControllerClient::handleProvisioningRequests() { if (!provisioningMode_) { webServer.send(200, "application/json", "{\"ok\":true}"); return; } const int count = WiFi.scanComplete(); String options = "<option value=\"\">Select a network</option>"; if (count > 0) for (int i = 0; i < count; ++i) { const String ssid = WiFi.SSID(i); if (!ssid.isEmpty()) options += "<option value=\"" + htmlEscape(ssid) + "\">" + htmlEscape(ssid) + "</option>"; } const String html = String("<!doctype html><html><meta name='viewport' content='width=device-width,initial-scale=1'><title>ESP32 LED Blink</title><body style='font-family:system-ui;max-width:520px;margin:40px auto;padding:20px'><h1>ESP32 LED Blink</h1><p>Device: <b>") + htmlEscape(deviceId_) + "</b></p><p>Firmware: <b>" + htmlEscape(firmwareVersion_) + "</b></p><p>Device key is available in Serial Monitor.</p><form method='post' action='/save'><label>Wi-Fi</label><br><select name='ssid' style='width:100%;padding:12px'>" + options + "</select><br><br><label>Password</label><br><input name='password' type='password' style='width:100%;padding:12px'><br><button style='margin-top:16px;padding:12px;width:100%'>Connect</button></form><form method='get' action='/scan'><button style='margin-top:10px;padding:12px;width:100%'>Scan again</button></form></body></html>"; webServer.send(200, "text/html", html); }

void ControllerClient::begin() { deviceId_ = makeDeviceId(); loadOrCreateDeviceKey(); Serial.printf("[CONTROLLER] URL: %s\n", controllerUrl_.c_str()); Serial.printf("[CONTROLLER] Device ID: %s\n", deviceId_.c_str()); Serial.printf("[CONTROLLER] Firmware: %s | Build: %s\n", firmwareVersion_.c_str(), buildId_.c_str()); if (!connectSavedWiFi()) startProvisioning(); startWebServer(); if (!provisioningMode_) { Serial.println("[CONTROLLER] Wi-Fi ready. Sending first heartbeat now..."); sendHeartbeat(); } }

void ControllerClient::queueAck(const String& id, const char* status, const String& result) { if (id.isEmpty()) return; if (pendingAcks_.length()) pendingAcks_ += ','; pendingAcks_ += "{\"id\":\"" + id + "\",\"status\":\"" + String(status) + "\",\"result\":\"" + result + "\"}"; }

void ControllerClient::processCommands(const String& json) {
  const int commandsStart = json.indexOf("\"commands\":[");
  if (commandsStart < 0) return;
  int cursor = commandsStart;
  while (true) {
    const int idPos = json.indexOf("\"id\":\"", cursor); if (idPos < 0) break;
    const String id = jsonValue(json, "id", idPos);
    const int typePos = json.indexOf("\"type\":\"", idPos); if (typePos < 0) break;
    const String type = jsonValue(json, "type", typePos);
    if (type == "message") {
      const int messagePos = json.indexOf("\"message\":\"", typePos);
      if (messagePos >= 0) { pendingMessage_ = jsonValue(json, "message", messagePos); Serial.printf("[COMMAND] Remote message: %s\n", pendingMessage_.c_str()); queueAck(id, "executed", "message_received"); }
    } else if (type == "ota") {
      const String tag = jsonValue(json, "tag", typePos), version = jsonValue(json, "version", typePos);
      Serial.printf("[COMMAND] OTA requested: %s (%s)\n", tag.c_str(), version.c_str());
      if (tag.isEmpty()) queueAck(id, "failed", "missing_tag");
      else if (performOta(tag, version)) return;
      else queueAck(id, "failed", "ota_failed");
    } else queueAck(id, "rejected", "unsupported_command");
    const int nextId = json.indexOf("\"id\":\"", typePos + 8); if (nextId < 0) break; cursor = nextId;
  }
}

bool ControllerClient::performOta(const String& tag, const String& version) {
  if (version == firmwareVersion_) { Serial.println("[OTA] Already running requested version."); return false; }
  HTTPClient http;
  const String url = controllerUrl_ + "/api/device/firmware/download/" + tag + "?deviceId=" + deviceId_;
  if (!http.begin(url)) { Serial.println("[OTA] HTTP initialization failed."); return false; }
  http.setConnectTimeout(10000); http.setTimeout(30000); http.addHeader("X-Device-Key", deviceKey_);
  const int status = http.GET();
  if (status != HTTP_CODE_OK) { Serial.printf("[OTA] Download failed: HTTP %d | %s\n", status, http.getString().c_str()); http.end(); return false; }
  const int contentLength = http.getSize();
  if (contentLength <= 0 || !Update.begin(contentLength)) { Serial.println("[OTA] Invalid firmware size or Update.begin failed."); http.end(); return false; }
  WiFiClient* stream = http.getStreamPtr(); const size_t written = Update.writeStream(*stream);
  if (written != static_cast<size_t>(contentLength) || !Update.end(true)) { Serial.printf("[OTA] Update failed. Written=%u expected=%d\n", static_cast<unsigned>(written), contentLength); Update.abort(); http.end(); return false; }
  Serial.println("[OTA] Update complete. Restarting..."); http.end(); delay(500); ESP.restart(); return true;
}

void ControllerClient::sendHeartbeat() {
  if (provisioningMode_ || WiFi.status() != WL_CONNECTED) return;
  if (lastHeartbeatAt_ != 0 && millis() - lastHeartbeatAt_ < HEARTBEAT_INTERVAL_MS) return;
  lastHeartbeatAt_ = millis();
  HTTPClient http; const String url = controllerUrl_ + "/api/device/heartbeat";
  if (!http.begin(url)) { Serial.println("[HEARTBEAT] HTTP begin failed; retrying soon."); lastHeartbeatAt_ = 0; return; }
  http.setConnectTimeout(10000); http.setTimeout(10000); http.addHeader("Content-Type", "application/json"); http.addHeader("X-Device-Key", deviceKey_); http.addHeader("User-Agent", "ESP32-LED-Blink-Central-Test/" + firmwareVersion_);
  String body = "{\"deviceId\":\"" + deviceId_ + "\",\"firmwareVersion\":\"" + firmwareVersion_ + "\",\"buildId\":\"" + buildId_ + "\",\"hardware\":\"esp32\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"uptime\":" + String(millis());
  if (pendingAcks_.length()) { body += ",\"commandAcks\":[" + pendingAcks_ + "]"; pendingAcks_.clear(); }
  body += "}";
  Serial.printf("[HEARTBEAT] POST %s\n", url.c_str());
  const int status = http.POST(body); const String response = http.getString();
  if (status == HTTP_CODE_OK) { Serial.printf("[HEARTBEAT] HTTP 200 | %s\n", response.c_str()); processCommands(response); }
  else { Serial.printf("[HEARTBEAT] FAILED HTTP %d | %s\n", status, response.c_str()); lastHeartbeatAt_ = millis() - (HEARTBEAT_INTERVAL_MS - FIRST_HEARTBEAT_RETRY_MS); }
  http.end();
}

void ControllerClient::loop() { if (provisioningMode_) dnsServer.processNextRequest(); webServer.handleClient(); if (WiFi.status() == WL_CONNECTED) sendHeartbeat(); else if (!provisioningMode_ && millis() - lastWiFiRetryAt_ >= WIFI_RETRY_INTERVAL_MS) { lastWiFiRetryAt_ = millis(); WiFi.reconnect(); } }

bool ControllerClient::provisioningMode() const { return provisioningMode_; }
const String& ControllerClient::deviceId() const { return deviceId_; }
const String& ControllerClient::deviceKey() const { return deviceKey_; }
bool ControllerClient::consumeMessage(String& message) { if (pendingMessage_.isEmpty()) return false; message = pendingMessage_; pendingMessage_.clear(); return true; }
