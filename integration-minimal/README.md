Integration-minimal: add these minimal files to your ESP32 project to enable remote control and OTA via the ESP32 Universal Controller

Overview

This folder contains the minimal files and instructions to integrate the ControllerClient into other ESP32/PlatformIO projects so devices can be remotely controlled and receive OTA updates from the Universal Controller backend.

Contents

- include/controller_client.h  : Header for the ControllerClient (API surface)
- src/controller_client.cpp    : Minimal implementation stub (see note)
- include/version.h            : Template version header (update as needed)
- version.py                   : PlatformIO pre-build script to inject version/build macros
- README.md                    : This file

Important note

The src/controller_client.cpp in this integration package is a minimal stub that contains the queue/task scaffolding and API stubs. To enable full OTA/heartbeat functionality you must copy the full implementation from the LED Blink Central Test project's src/controller_client.cpp (or replace the stub with the full file). The full implementation includes the OTA download, Update.* usage, heartbeat, and progress reporter.

Quick integration steps

1. Copy files
   Copy the folder contents into your project (keep relative paths):
     integration-minimal/include/controller_client.h -> <yourproject>/include/controller_client.h
     integration-minimal/src/controller_client.cpp -> <yourproject>/src/controller_client.cpp  (replace with full implementation if needed)
     integration-minimal/version.py -> <yourproject>/version.py
     integration-minimal/include/version.h -> <yourproject>/include/version.h (optional)

2. platformio.ini
   Add/verify these entries in platformio.ini:
     extra_scripts = pre:version.py
   And set the controller URL via build_flags, for example:
     build_flags =
       -DUEC_CONTROLLER_URL=\"https://esp32-universal-controller.onrender.com\"

3. main usage
   Example usage in your main.cpp:
     #include "controller_client.h"
     ControllerClient controller(UEC_CONTROLLER_URL, UEC_FIRMWARE_VERSION, UEC_BUILD_ID);
     void setup() { controller.begin(); }
     void loop() { controller.loop(); }

4. Build & run
   - Build and flash with PlatformIO. The device should log its device key on first run and POST heartbeats to the controller once WPA credentials are saved.

FAQ

Q: Do I need the full controller_client.cpp?
A: Yes. The stub provided here is a scaffold. To use OTA and progress reporting as in the LED Blink project you must copy the full implementation from the upstream project's src/controller_client.cpp.

Q: Can I keep device keys unique per device?
A: Yes — the client stores a device key in Preferences and the server maps that to a device record. Do not copy a single device key across many devices.

Support

If you want, I can:
- Replace the stub with the full implementation in this integration folder (so the folder becomes a drop-in package),
- Create a small example main.cpp demonstrating full integration, or
- Add a single-file combined implementation that keeps everything in one file for easier copy-paste.

Tell me which one you prefer and I'll apply it.