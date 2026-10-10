# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Firmware for a squash scoreboard display. Written in C++ using PlatformIO and the Arduino framework. Supports squash, volleyball, and padel scoring. **One source tree builds two boards**, selected by `-DBOARD_REV`:

| | V1 (`v1` / `v1_ota`, `BOARD_REV=1`) | V2 (`v2`, `BOARD_REV=2`) |
|---|---|---|
| Board | ESP32-S3-DevKitC-1 N16R8 | ESP32-S3-DevKitC-1 N16R8 |
| Front | 112 WS2812B: 4 seven-segment digits, colon, player indicators, 24-LED history bar | 74 slots: 4 nine-segment digits + split centre border around a 2.9" e-paper |
| Back | OLED (+ battery percent in the menus) | OLED + 2 player indicator LEDs |
| Extra | battery voltage sense | battery voltage sense |

**Both boards are the same module on one pinout**; `BOARD_REV` means "which front panel".
Board settings live in platformio.ini's `[s3]` section.

## READ FIRST: two things that will break this device

### 1. The platform is PINNED. Do not upgrade it.

```ini
platform = espressif32@6.13.0    ; Arduino core 2.0.17, GCC 8.4
```

Moving to Arduino core 3.x / IDF 5.x **corrupted the LED output**: wrong colours
*and* wrong shapes, independent of WiFi (tried and rolled back 2026-09-04). Core 3.x on
the S3 is **untested** (task #74), so the pin stays until that study passes on both boards.

Cause: FastLED silently picks its RMT driver from the IDF version. The RMT4/RMT5 table, the **FastLED 3.9.16 pin** and the WiFi LED-glitch mechanism are in `.claude/rules/platform.md` - read it before touching the platform, FastLED, the LED driver or any WiFi/AP path.

### 2. V1: OTA is the only practical way to flash. USB needs disassembly.

(V2 is on the bench and flashes over native USB. Never flash V1 casually.)

V1's one USB flash was the `v1_bootstrap` image on the naked board (2026-09-30); from then on it is `v1_ota` only. A bad image means taking
the unit apart. Before any toolchain, platform, or LED library change, run these checks
**before** uploading:

| Check | Why |
|---|---|
| `partitions.bin` old vs new is identical | OTA writes only the app image; the device keeps its own table. A changed NVS offset loses WiFi credentials and the device falls back to AP mode, unreachable. |
| New image fits the app slot | `app0` and `app1` are 6.25 MB each (`default_16MB.csv`). V1 is ~920 KB (14%), V2 ~963 KB. |
| Board marker is `Ok` | `python helpers/check_firmware_image.py .pio/build/v1_ota/firmware.bin v1` must print `verdict Ok`. V1 **refuses unmarked images**, so an image whose marker failed to link is refused - and a V1 running such firmware would refuse every correct image after it. |
| Failure mode is safe | `Update.end(true)` switches the boot partition **only on success**, so a corrupt or partial upload leaves the running firmware bootable. |

Then verify with a **two-cycle OTA test**: flash the new firmware, then flash
*again*. The second cycle is the one that matters - it proves the new firmware can
still *receive* updates. Firmware that boots but cannot be updated is the
unrecoverable case, and static review will not catch it.

## Build & Flash Commands

Run from Windows PowerShell; `pio` is on PATH and the working directory is
already the project root.

```powershell
pio run -e v1 -e v2                     # ALWAYS build both after a change
pio run -t upload -e v2                 # V2: flash via native USB
pio run -e v1                           # build V1 only
pio run -t upload -e v1_ota             # V1: flash via OTA - normal path
pio run -t upload -e v1 --upload-port COMx          # V1 via USB - needs disassembly
pio run -t upload -e v1_bootstrap --upload-port COMx # naked board only, see below
pio device monitor                      # USB CDC serial, 115200 (both boards)
```

The `v1_bootstrap` env is described in `.claude/rules/persistence-network.md`, `v1_ota_rmtdiag` in `platform.md`.

- The S3's USB-Serial/JTAG maps DTR/RTS to BOOT/EN: opening or closing a terminal can park
  the chip in `waiting for download`. Reset with `esptool.py --chip esp32s3 -p COMx --after
  hard_reset read_mac`. HW CDC's `Serial` is false (logs suppressed) unless DTR is held.
- **Never commit machine- or network-local values** - IPs, MACs, COM ports, SSIDs, user paths -
  not in config, docs or comments: the repo is public. Ports and the V1 OTA address live in the
  gitignored `platformio.local.ini` (template `platformio.local.ini.example`, loaded through
  `[platformio] extra_configs`); without it, pass `--upload-port`. The V1's DHCP address can
  change; find it via the USB log's `WiFi: connected ... IP` line.

Verify an OTA landed: `curl http://<v1-ip>/api/device` returns `{"fw":...,"ssid":...,"hw":"V1 ESP32-S3"}`
from any screen - compare `fw` with `version.txt` (the gated JSON `/api/roster` and
`/api/settings` answer 503 outside PROFILE and Dev Mode). Port 23 is the telnet log. The device reboots after an upload and takes
~20-40 s to rejoin, so poll rather than assuming failure.

There is no on-device test suite. Host tests are PlatformIO Unity suites under `test/`,
run on Windows - run them after touching `src/Display/LedDisplay/`, `src/Tournament/`
or anything else they cover:

```powershell
pio test -e native_v1 -e native_v2        # all suites, both boards (-f test_<name> for one)
python helpers/preview_glyphs.py check     # glyph table + 7-segment collapse vs V1
python helpers/led_positions.py v1 --check; python helpers/led_positions.py v2 --check
```

- **`[env]` sets `test_ignore = *` - never remove it.** Without it a bare `pio test` builds Unity images for the firmware envs and uploads them, to V1 over OTA included.

Logs: `printLn` goes to telnet (port 23) and, on both boards, to USB CDC (115200)
whenever a host is attached (`if (Serial)` in `LoggerHelper.h`).

A pre-build script (`helpers/version_increment.py`) auto-increments the firmware version on each build - once **per env**, so a two-env build bumps it twice. It tolerates a UTF-8 BOM in `version.txt` (a BOM once broke every build).

## Hard rules

Each has its reasoning in the rule file named after it.

- **`#if BOARD_REV` only in `src/Board.h` and hardware wrapper headers** (`DisplayProfile.h`, `LedDisplay.h`, `LedCentralScreenBorder.h`, `Animation/LedSweepAnimation.h`, `Animation/LedSlotPositions.h`, `EInk/EInkDisplay.h`, `EInk/Images/PlayerSetupQr.h`, `EInk/Images/Bajgiel.h`, `BatterySensor.h`). Never in views, modes, `Tournament`, `Match`, `Game`, `Rules`. Wrappers keep identical APIs on both boards (empty stubs on V1).
- **The language standard is C++11** (`-std=gnu++11`, set by the pinned Arduino core's builder, GCC 8.4). Do not raise it: under `-std=gnu++14` some libraries no longer compile. So no C++14 features - `src/Utils.h` backfills `std::make_unique`, and every file that calls it must include `Utils.h` itself rather than rely on another header having pulled it in.
- `static constexpr` arrays as class members in header-only adapters cause ODR linker errors under C++11 (no inline variables). Declare them as local `constexpr` variables inside the static method instead.
- Always use `match->getLeftCourtSidePlayer()` / `getRightCourtSidePlayer()` in views for display positioning. Never use `getPlayerA()` / `getPlayerB()` directly — those ignore the court-side swap state.
- Never call `FastLED.show()` from a render path - every frame goes through `showCompensated()` (`led-engine.md`).
- Never call `ESP.restart()` directly - use `safeRestart()` (`input-buzzer.md`).
- Every AP path goes through `RemoteDevelopmentService::startApOnly()`, and `-DFASTLED_RMT_MEM_BLOCKS=4` stays a build flag: a retrying STA glitches the LEDs (`platform.md`).
- Never grow or add a field to `PrefsData`: its size is the stored layout, and a mismatch drops brightness and the WiFi credentials. New persisted data gets its own NVS key in namespace `"ns"` (`persistence-network.md`, `roster-web.md`).
- Never add an erase/clear/remove of a roster blob (`roster-web.md`).
- Every user-visible string lives in `src/Strings.h`, both language branches, no diacritics; LED words have no W, K, M or V (`strings.md`).
- Set brightness only through `LedDisplay`, never `FastLED.setBrightness` (`battery.md`).
- Never write a raw NUL into a source file from a script (`eink.md`).

## Architecture

### DeviceMode + View pattern
- `DeviceMode` (abstract) — owns a state machine and an active `View`. Each mode has its own state enum (e.g., `SquashModeState`). When the state changes, a new `View` is instantiated. `activeView` lives in the **base class**, not in each mode, so `restoreView()` (re-run the view's three `init*Display` hooks + `queueRender()`) works without knowing which mode is active.
- `View` (abstract) — each frame it calls `handleInput()`, `renderLedDisplay()` (front LEDs), and `renderBackDisplay()` (rear OLED). Flags `shouldRenderLedDisplay`/`shouldRenderBack` control dirty rendering.

## Rule files

Subsystem detail lives in `.claude/rules/`. Each file loads automatically when a file matching the `paths:` globs in its frontmatter is read or edited, in the main session and in subagents alike. Read one yourself when a task touches its topic without opening its files (e.g. only running `pio`). New knowledge goes into the matching rule file; this file holds only what every task needs. After adding, moving or renaming rule files or the source they cover, run `python helpers/check_claude_rules.py`.

| File | Covers |
|---|---|
| `platform.md` | RMT4/RMT5 mechanism, FastLED pin and replacement options, WiFi LED glitches, `v1_ota_rmtdiag`, pioarduino toolchain gotcha |
| `tests.md` | native toolchain, suites, fixtures and rebaselining |
| `modes-views.md` | `setup()`/`loop()`, Overlay, view flow, intro/celebration views, mode transitions, mode selector |
| `led-views.md` | LED calls views make: bar lambda, celebration vs reset, layer owners, breathing/on-fire/comeback order, border, ids `% 10` |
| `led-engine.md` | sweep, layer engine, smoke, intro, brightness compensation, glyph tables, LED slot maps |
| `tournament.md` | game ball, on fire, comeback, commit timeout, padel two-level scoring and tiebreak |
| `eink.md` | e-paper driver, match screen, fonts, menus, footer, ghosting, images |
| `oled.md` | dead rows, text clamp, menu rows, navbar, battery icon |
| `input-buzzer.md` | 433 MHz receiver measurements, long C, buzzer modes, `safeRestart()` |
| `garmin.md` | NimBLE pin, `src/Garmin/` core, `GarminService`, watch commands, CONFIG GARMIN screens |
| `persistence-network.md` | `PrefsData`, Dev Mode, apply-on-save, OTA image check and board marker, `v1_bootstrap`, core-3.x fixes |
| `strings.md` | one language per build, no diacritics, LED word limits, numerals |
| `roster-web.md` | two rosters, NVS blob rules, uid vs id, palette, web pages, routes and gate, settings page, validators |
| `battery.md` | sensor, plausibility, percent curve, low state, brightness cap |
| `board.md` | pinout table, pin safety, bench power |
