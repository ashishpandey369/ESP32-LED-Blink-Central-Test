# ESP32 LED Blink — Central Management Test

A clean ESP32 test project for validating the reusable ESP32 Universal Controller device-management layer.

## What this project tests

- Permanent ESP32 device identity and device key
- Wi-Fi provisioning through the ESP32 setup AP
- Automatic heartbeat registration
- Device ownership/pairing through the central dashboard
- Online/offline presence
- Firmware version and build reporting
- Remote messages delivered to the ESP32 Serial Monitor
- OTA command plumbing for future firmware releases
- Separation between application code and controller-management code

## Project identity

- Project ID: `led-blink-central-test`
- Firmware: `0.1.0`
- Build: `led-blink-test-v0.1.0`
- Hardware: ESP32
- Controller: `https://esp32-universal-controller.onrender.com`

## Build

Open this repository in VS Code with PlatformIO and run:

```text
pio run
pio run -t upload
pio device monitor -b 115200
```

On first boot the ESP32 creates a permanent device key and starts its Wi-Fi provisioning AP. The Serial Monitor prints the device ID, device key, AP name, AP password, and setup URL.

After Wi-Fi is configured, the ESP32 sends heartbeats to the central controller every 30 seconds and should appear automatically after it is paired in the dashboard.

## Application

The application itself only blinks `LED_BUILTIN` once per second. The controller client lives separately in `include/controller_client.h` and `src/controller_client.cpp`, demonstrating the intended modular integration pattern for future projects.

## Security

Do not commit Wi-Fi passwords, device keys, GitHub tokens, Supabase credentials, or other secrets.
