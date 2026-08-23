#pragma once

// Minimal template version header for integration builds.
// You can either keep this file and update the values, or use version.py
// together with 'extra_scripts = pre:version.py' in platformio.ini to set
// the version and build id dynamically from your CI/tags.

#define UEC_FIRMWARE_VERSION "0.5.6"
#define UEC_BUILD_ID "led-blink-central-test::0.5.6"
#define UEC_PROJECT_ID "led-blink-central-test"
#define UEC_HARDWARE_TARGET "esp32"
