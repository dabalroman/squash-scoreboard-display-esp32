---
paths:
  - "src/PrefsData.h"
  - "src/PreferencesManager.h"
  - "src/RemoteDevelopmentService/**"
  - "src/FirmwareImageCheck.h"
  - "src/BoardMarker.*"
  - "helpers/check_firmware_image.py"
  - "helpers/wifi_bootstrap.py"
  - "test/test_firmware_image/**"
  - "test/test_prefs_bootstrap/**"
---

# Persistence and networking

- `PreferencesManager` — reads/writes `PrefsData` (WiFi SSID/password, brightness, AP mode) to ESP32 NVS. `wifiIpAddress` is **live state, never persisted**: empty until an interface comes up, set from `WiFi.localIP()` on an STA connect and `WiFi.softAPIP()` in both AP paths, cleared when WiFi is off or an AP drops. The CONFIG footer omits its line while it is empty, which is why it defaults to empty rather than to a placeholder.
- **`enableDevMode` defaults to 0.** A device with empty or invalid NVS boots with STA, AP, OTA and telnet all off, instead of spending ~15 s failing STA against an empty SSID and then raising an open AP. Stored blobs keep their own value, so V1's OTA path is untouched. Recovery on a wiped device is CONFIG -> Dev Mode ON -> `[RESTART]`, from the remote alone.
  - The CONFIG row is labelled **Dev Mode**, not WiFi: all it exclusively buys is joining the house network at boot, and PROFILE raises its own AP regardless, so "WIFI" read as if the roster editor needed it. The label is the same in both language branches. It is the one CONFIG row with **no LED word** - the 46-glyph table has no W, K, M or V, so neither DEV nor MODE can render; the indicators' green/red carries the state. On the e-paper the label is 2 px wider than the tickbox row allows and is clipped, deliberately - clip-never-shrink.
- **Apply-on-save choke point.** `PreferencesManager::save()` calls `apply()` after `putBytes`; `main.cpp`'s `applySettings` sets brightness and the buzzer. Every writer (CONFIG exit, the web settings, `/connect`, `/settings/dev`) is therefore live without applying by hand - the CONFIG buzzer toggle no longer needs a reboot. `setup()` calls `apply()` once, after `initHardware()` and before the boot sweep. Dev Mode is deliberately not applied (network topology, read at boot and by `disablePlayerSetupAp()`); the handler must never block, restart or touch WiFi.
- `RemoteDevelopmentService` logs the boot WiFi outcome (IP or fallback AP): a naked or sealed board has no OLED to read it from.
- **OTA image check** (`FirmwareImageCheck::Accumulator`, on `POST /update`) judges the first 296 bytes: 0xE9, chip id = sdkconfig's (S3 on both boards, so it only rejects other chips), app descriptor magic, then the **board marker** at file offset 0x120. The marker is `board_marker` (`src/BoardMarker.*`, magic `0xB0A2D5E7` + `BOARD_REV`) in `.rodata_custom_desc`, which IDF 4.4's `sections.ld` places straight after `esp_app_desc_t`. `-Wl,-u,board_marker` in `[s3]` keeps it past `--gc-sections` - never remove it. Other board's rev -> WrongBoard; no marker -> accepted only on V2 (`LEGACY_UNMARKED_REV`, pre-marker V2 builds), MissingMarker on V1; under 296 bytes -> NotFirmware. `?force=1` writes past any verdict. Host suite `test_firmware_image`; `helpers/check_firmware_image.py <bin> v1|v2` mirrors the device's verdict. Verified on the naked V1: the V2 image got HTTP 400.
- **This file is already Arduino-core-3.x-ready.** Two fixes were applied on
  2026-09-04 and are valid on *both* cores, so do not revert them if the platform
  is ever moved:
  1. `RemoteDevelopmentService.h` — `#include <WiFi.h>`. Core 2.0.17's
     `WebServer.h` transitively provided `WiFiServer`/`WiFiClient`; core 3.x pulls
     `NetworkServer`/`NetworkClient` instead. This one include fixes every such error.
  2. `RemoteDevelopmentService.cpp` — `WiFiClass::status()` became `WiFi.status()`.
     Calling a non-static member without an object was always invalid; GCC 8.4
     tolerated it, GCC 14 does not.
  With those in place the firmware builds clean on core 3.3.11 for
  `esp32-s3-devkitc-1`. It is the LED driver, not the
  networking code, that blocks the upgrade.

## The `v1_bootstrap` env

- `v1_bootstrap` is the first-flash image of a **naked** V1 (empty NVS, no remote to reach
  Dev Mode): `helpers/wifi_bootstrap.py` reads `SCOREBOARD_WIFI_SSID` /
  `SCOREBOARD_WIFI_PASSWORD` from the environment (build fails if unset or over 63 bytes)
  into `.pio/build/v1_bootstrap/bootstrap/BootstrapWifi.generated.h`, on that env's include
  path only. `PreferencesManager::read()` seeds them with `enableDevMode = 1` **only when
  no valid blob exists** (`PrefsBootstrap::seed`, `test_prefs_bootstrap`). No other image
  contains the credentials. Delete that build dir afterwards; never upload it over OTA.
