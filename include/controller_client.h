#pragma once

#include <Arduino.h>

class ControllerClient {
 public:
  ControllerClient(const char* controllerUrl, const char* firmwareVersion, const char* buildId);

  void begin();
  void loop();
  bool provisioningMode() const;
  const String& deviceId() const;
  const String& deviceKey() const;
  bool consumeMessage(String& message);

 private:
  String controllerUrl_;
  String firmwareVersion_;
  String buildId_;
  String deviceId_;
  String deviceKey_;
  String pendingMessage_;
  bool provisioningMode_ = false;
  unsigned long lastHeartbeatAt_ = 0;
  unsigned long lastWiFiRetryAt_ = 0;

  void loadOrCreateDeviceKey();
  bool connectSavedWiFi();
  void startProvisioning();
  void handleProvisioningRequests();
  void startWebServer();
  void sendHeartbeat();
  void processCommands(const String& json);
  void performOta(const String& tag, const String& version);
  String chipIdHex() const;
  String generateDeviceKey() const;
  String makeDeviceId() const;
};
