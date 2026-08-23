# Development Baseline

## Frozen known-good baseline

Repository: `ashishpandey369/ESP32-LED-Blink-Central-Test`

Baseline commit: `57c56b2b79ce6ebe1019463bacc93504099deeae`

Commit: `Improve OTA stream read tolerance`

### Baseline characteristics

- Working LED Blink central test controller implementation.
- OTA stream read timeout is 5000 ms.
- Existing heartbeat, OTA, Wi-Fi, provisioning, recovery portal, command processing, and LED behavior are preserved.

## Change-control rules

1. Do not rewrite or reconstruct `src/controller_client.cpp` from partial/truncated content.
2. Use the frozen baseline above as the source of truth for future controller changes.
3. Every requested change must be isolated to the specific requested behavior.
4. Do not add duplicate implementations or duplicate services/tasks.
5. Before merging, compare the proposed commit against the baseline and verify only intended files/lines changed.
6. A large replacement diff for a targeted change is a hard stop and must not be merged.
7. For development-only device-key logging, the intended design is a compile-time debug guard around the existing key print; production builds must be able to disable that logging.
8. Do not change heartbeat/OTA/networking/provisioning/LED behavior when making the device-key logging change.

## Current requested development change

Add development-only visibility for the existing persistent device key in Serial Monitor. Do not change key generation, storage, authentication, heartbeat behavior, OTA behavior, or application behavior.
