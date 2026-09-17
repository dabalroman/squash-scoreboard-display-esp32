# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Firmware for a squash scoreboard display. Written in C++ using PlatformIO and the Arduino framework. Supports squash, volleyball, and padel scoring. **One source tree builds two boards**, selected by `-DBOARD_REV`:

| | V1 (`lolin_s2_mini`, `BOARD_REV=1`) | V2 (`esp32s3_devkitc`, `BOARD_REV=2`) |
|---|---|---|
| Board | Wemos S2 Mini (ESP32-S2) | ESP32-S3-DevKitC-1 N16R8 |
| Front | 112 WS2812B: 4 seven-segment digits, colon, player indicators, 24-LED history bar | 74 slots: 4 nine-segment digits + split centre border around a 2.9" e-paper |
| Back | OLED | OLED + 2 player indicator LEDs |
| Extra | - | battery voltage sense |

`V2 Guidelines.md` (untracked, user-maintained) holds the V2 design record and hardware facts.

## READ FIRST: two things that will break this device

### 1. The platform is PINNED. Do not upgrade it.

```ini
platform = espressif32@6.13.0    ; Arduino core 2.0.17, GCC 8.4
```

Moving to Arduino core 3.x / IDF 5.x **corrupts the LED output**: wrong colours
*and* wrong shapes, independent of WiFi. Tried and rolled back on 2026-09-04.

Cause: FastLED silently picks its RMT driver from the IDF version, with no way to
override it.

| | RMT4 (core 2.0.17, works) | RMT5 (core 3.x, breaks) |
|---|---|---|
| ISR owner | FastLED's own, tight, IRAM | generic IDF `rmt_tx` + encoder callback |
| Refill buffer | `FASTLED_RMT_MEM_BLOCKS 2` = **128 symbols** | `mem_block_symbols = 0` -> default **64** |
| Runway before starving | ~160 us | **~80 us** |

Half the buffer and a heavier refill path, against this board's four RF-receiver
GPIO ISRs (which fire in *bursts* from RF noise - see Input below) plus I2C.
Refills land late, the RMT starves mid-frame, and the WS2812 stream shifts, so
every downstream LED receives its neighbour's bytes.

There is **no escape hatch**:
- `-DFASTLED_RMT5=0` does not compile. FastLED's RMT4 path is unimplemented for
  ESP32-S2 on IDF 5.x (`"doneOnChannel not yet implemented for ESP32-S2 in idf 5.x"`).
- RMT5 has no tuning knobs. `memory_block_symbols = with_dma ? 1024 : 0` is
  hardcoded in `rmt_5/strip_rmt.cpp`, and `.with_dma = false` is hardcoded too.

**FastLED is also pinned at 3.9.16.** Neither newer release builds here: 3.10.4
fails in `fl/gfx/crgb.h`, 3.10.3 fails on `fl::fl_map`, and installing 3.10.3 from
the registry crashes PlatformIO's library manager. The registry is stale at 3.10.3
anyway; upstream GitHub has 3.10.4.

### 2. V1: OTA is the only practical way to flash. USB needs disassembly.

(V2 is on the bench and flashes over native USB, COM8. Never flash V1 casually.)

A bad image means taking the unit apart. Before any toolchain, platform, or LED
library change, run these checks **before** uploading:

| Check | Why |
|---|---|
| `partitions.bin` old vs new is identical | OTA writes only the app image; the device keeps its own table. A changed NVS offset loses WiFi credentials and the device falls back to AP mode, unreachable. |
| New image fits the app slot | `app0` and `app1` are 1280 KB each. Current build: 65% (852,682 bytes as reported by `pio run`; the `.bin` on disk is 853,040). Core 3.x pushed this to 81%. |
| Failure mode is safe | `Update.end(true)` switches the boot partition **only on success**, so a corrupt or partial upload leaves the running firmware bootable. |

Then verify with a **two-cycle OTA test**: flash the new firmware, then flash
*again*. The second cycle is the one that matters - it proves the new firmware can
still *receive* updates. Firmware that boots but cannot be updated is the
unrecoverable case, and static review will not catch it.

## Build & Flash Commands

Run from Windows PowerShell; `pio` is on PATH and the working directory is
already the project root.

```powershell
pio run -e lolin_s2_mini -e esp32s3_devkitc   # ALWAYS build both after a change
pio run -t upload -e esp32s3_devkitc       # V2: flash via native USB (COM8)
pio run -e lolin_s2_mini                   # build V1 only
pio run -t upload -e lolin_s2_mini         # flash via USB (COM4) - needs disassembly
pio run -t upload -e lolin_s2_mini_ota     # flash via OTA (192.168.0.129) - normal path
pio device monitor                         # serial monitor (COM3, 115200)
```

Verify an OTA landed: `curl http://192.168.0.129/` should return the config page,
and port 23 is the telnet log. The device reboots after an upload and takes
~20-40 s to rejoin, so poll rather than assuming failure.

There is no on-device test suite. Host-side checks for the LED layer run in WSL
(`helpers/led_dump/README.md`) - run them after touching `src/Display/LedDisplay/`:

```bash
# from helpers/led_dump inside WSL Ubuntu-24.04
./run.sh --root ../../src --defines "-DBOARD_REV=1 -DLEDBAR_LAMBDA -DCONFIG_IDF_TARGET_ESP32S2=1"
./check_v2.sh
```
```powershell
python helpers/preview_glyphs.py check     # glyph table + 7-segment collapse vs V1
```

`run.sh` proves V1 renders byte-identically to golden dumps taken before the V2
work; `check_v2.sh` checks V2 slot invariants (bounds, dead slots, border, indicators).

Logs: `printLn` goes to telnet (port 23); on V2 it is also mirrored to USB serial
(COM8, 115200) whenever a host is attached (`Board::SERIAL_LOG`).

**Toolchain gotcha, only relevant if this repo is ever moved off 6.13.0:** the
`pio` on PATH runs under Python 3.10, while `~/.platformio/penv` is Python 3.14.
The pioarduino platform's builder imports `littlefs`, whose compiled extension is
`cp314`, so PATH `pio` dies with `ImportError: cannot import name 'lfs'`. Use
`& "C:/Users/rd/.platformio/penv/Scripts/pio.exe"` in that case. **This does not
affect the current 6.13.0 setup** - PATH `pio` is verified working here.

A pre-build script (`helpers/version_increment.py`) auto-increments the firmware version on each build - once **per env**, so a two-env build bumps it twice. It tolerates a UTF-8 BOM in `version.txt` (a BOM once broke every build).

## Architecture

### Top-Level Loop (`src/main.cpp`)
Hardware is initialized in `setup()`. The main `loop()` runs at ~20fps (50ms tick). It polls `RemoteInputManager` for input and delegates to the active `DeviceMode`. `einkDisplay.update()` and `batterySensor.loop()` run on **every** pass, before the 50 ms gate (the e-paper polls its BUSY pin). No custom FreeRTOS tasks.

### DeviceMode + View pattern
- `DeviceMode` (abstract) — owns a state machine and an active `View`. Each mode has its own state enum (e.g., `SquashModeState`). When the state changes, a new `View` is instantiated.
- `View` (abstract) — each frame it calls `handleInput()`, `renderLedDisplay()` (front LEDs), and `renderBackDisplay()` (rear OLED). Flags `shouldRenderLedDisplay`/`shouldRenderBack` control dirty rendering.
- `renderEInkDisplay(EInkDisplay&)` is the third, non-pure hook (default: blank). It is called every frame, but the e-paper must refresh only on real change: views pass **values** to `EInkDisplay` (`showMatchScore`, `showBlank`), which compares them with what is shown. Never rely on `queueRender()` for it (GamePlaying views never call it). Start each override with `if (!einkDisplay.available()) return;` so V1 computes nothing for its stub.
- View flow per sport mode: `TournamentChoosePlayers` → `MatchStartGame` → `GamePlaying` → `GameOver` → back to `MatchStartGame`.
- Modes: `ModeSwitchingMode` (menu), `ConfigMode`, `SquashMode`, `VolleyballMode`, `PadelMode`
- Mode transitions happen via a callback `onDeviceModeChange` passed down from `main.cpp`.
- Views that need per-tick updates (blinking) must NOT guard `renderLedDisplay` with `if (!shouldRenderLedDisplay)` — that flag prevents every-tick rendering. Only use the guard for purely event-driven views.
- Always use `match->getLeftCourtSidePlayer()` / `getRightCourtSidePlayer()` in views for display positioning. Never use `getPlayerA()` / `getPlayerB()` directly — those ignore the court-side swap state.

### Match / Tournament / Rules
- All tournament/match/game code lives under `src/Tournament/`.
- `Tournament` owns a collection of `Match`es and tracks the active one. It also holds `MatchOrderKeeper` for round-robin scheduling.
- `Match` owns `Game`s. Each `Game` holds per-game scores and delegates win detection to a `Rules` instance.
- `GameResult` stores slim post-game results. `winnerPlayerId` is a `uint8_t` player ID (not `GameSide`) — resolved at write-time in `Match::finishGame()` respecting swap state.
- `Rules` (abstract) — `checkWinner(scoreA, scoreB)` returns `GameSide`. Implementations: `SquashRules`, `VolleyballRules`, `ShortVolleyballRules`, `PadelRules`. `Rules` also exposes `historyReserve()` so each sport pre-sizes its `GameScoreHistory` (Squash 32 default, Volleyball 64, Padel 16).
- Score changes are "uncommitted" until 4 seconds of inactivity (each GamePlaying view's local `COMMIT_TIMEOUT_MS = 4000`), then `game->commit()` is called.

### Padel scoring (two-level history)
- Padel maps its three-level structure onto the two-level engine without touching the shared model: the engine `Game` **is a set** (its score = gems won, its history entries = gems), and a separate `PadelGemScorer` (`src/Tournament/Game/`) tracks the within-gem 0/15/30/40/Ad ladder **below** the `Rules` abstraction.
- `PadelGemScorer` is backed by its own `GameScoreHistory` (rallies), mirroring how `Game` uses one. So there are two independent history levels: gems (on the engine `Game`, shown on the LED bar) and rallies (in the scorer).
- `PadelGamePlayingView` keeps a stack of finished gems' rally histories so undo can step back across gem boundaries. When a gem completes it hand-couples the levels: `game->scorePoint(gemWinner)` + snapshot the gem; step-back reverses both.

### Display
- **`#if BOARD_REV` only in `src/Board.h` and hardware wrapper headers** (`DisplayProfile.h`, `LedDisplay.h`, `LedCentralScreenBorder.h`, `EInk/EInkDisplay.h`, `BatterySensor.h`). Never in views, modes, `Tournament`, `Match`, `Game`, `Rules`. Wrappers keep identical APIs on both boards (empty stubs on V1).
- `ledDisplay` — wraps the WS2812B chain (`Board::LED_COUNT`: 112 on V1, 74 on V2). Exposes 4 digit glyphs (A–D), a colon (no LEDs on V2), two player indicators, and on V1 a 24-pixel history bar (`LedBar`, from index 88). Call `display()` to clear, render, and show in one step.
- **V2 has no history bar.** `setLedBarState` takes a **lambda** (`[&] { return XBarRenderer::toLedBarPixels(...); }`), never pixels: an argument is evaluated even into an empty setter. `resetHistoryBar`/`startCelebration` are no-ops on V2. Keep `LedBar::PIXEL_COUNT = 24` on both boards (`MatchResultBarRenderer` breaks at 0).
- **Border (V2)** — `LedCentralScreenBorder` is its own concept, independent of the back indicators: `LedDisplay::setBorderEnabled(bool)` and `setBorderAppearance(top, bottom, blinkTop, blinkBottom)`. The indicator methods never touch it. Colours are used **as passed**, never `sameSideMode`-redirected (border faces front; indicators face back). It is the legend for the e-paper rows.
  - Every LED view calls `setBorderEnabled` in `initLedDisplay` (no implicit default, it would leak across views): off in the menus (`ConfigView`, `ModeSwitchingView`, the three `*TournamentChoosePlayersView`s), on everywhere else with the same colours as the indicators.
- **E-paper (V2)** — `EInkDisplay` wraps `EInkAsync` (ported from the rig): non-blocking refresh state machine, ~10 ms SPI bursts, the ~0.5 s panel wait polled from `loop()`, requests coalesce. Driver class `GxEPD2_290_GDEY029T94` (GxEPD2 pinned 1.6.9); never the blocking `GxEPD2_BW` in the loop. Panel is mounted upside down → canvas rotation 2. Black is `INK`, white `PAPER` (`Adafruit_SSD1306.h` #defines `BLACK`/`WHITE`). Shows match-level score only: games won, or gems + sets in padel; top row = left court player. **`BackDisplay` and the e-ink share nothing** (no base class, helpers or interface).
  - Menus: views pass `EInkMenuRow`s to `showMenu()`; FreeSans 12 pt, selected row outlined (not filled), checkboxes for in/out. The scroll window is each renderer's own state - `Scrollable` holds only options + selection + wrap (the OLED `ScrollableWidget` keeps its 3-row window, the e-paper its own).
  - Ghosting: `EInkPolicy` - a screen-type change after >= 16 partials, or 128 partials in any case, becomes a non-blocking full refresh (~1.6 s flash, loop keeps running); every full refresh resets the counter.
  - Never write a raw NUL into a source file from a script (`'\\0'` in a Python string becomes a real NUL): the file still compiles but git treats it as binary.
  - Images: full-screen only. `helpers/eink_image.py <png>` converts a **128×296, pure black/white, pre-dithered** PNG (authored upright, no alpha) into a committed `PROGMEM` header under `src/Display/EInk/Images/`; the converter never thresholds or dithers, it rejects anything else. The generated header is committed so a build needs neither Python nor Pillow; the source PNG is **not** in git (`/assets/eink/` is ignored) - keep the artwork locally and rerun the script after changing it. The boot splash is one such image and holds the panel for `SPLASH_HOLD_MS` (4 s) — `show*` return early meanwhile — until `dismissSplash()`, which `main.cpp` calls on any remote press.
  - The firmware version is shown only in the CONFIG menu footer (second line, under the battery voltage), never on the splash.
- `BackDisplay` — wraps the rear 0.96" OLED (Adafruit SSD1306 128×64). Provides helper methods like `renderScoreWidget()`. Rotation is `Board::OLED_ROTATION` (V1 2, V2 0).
- Bar renderers live in `src/Display/LedDisplay/Renderer/`. Each exposes a static `toLedBarPixels()` returning `std::array<LedBarPixel, LedBar::PIXEL_COUNT>`.
- `static constexpr` arrays as class members in header-only adapters cause ODR linker errors with GCC 8.4 (C++14). Declare them as local `constexpr` variables inside the static method instead.
- **FastLED is a pinned, load-bearing dependency (3.9.16).** See *READ FIRST*.
  The project uses only `addLeds`, `show`, `clear`, `setBrightness`,
  `setMaxRefreshRate` and `CRGB` - **none** of its colour engine (no `CHSV`,
  palettes, blends). It is acting purely as a WS2812 shift-out driver, so replacing
  it is ~5 call sites plus a `CRGB` shim. Options are evaluated in the playground
  repo's CLAUDE.md; the leading candidate is NeoPixelBus with a **DMA (I2S)**
  method, because naming the peripheral explicitly avoids the silent
  driver-swap-by-IDF-version that caused the core 3.x regression. Not yet decided -
  only forced if V2's LED output misbehaves (V2 shares the pinned platform, so it
  also runs RMT4; the S3 fallback is `FASTLED_USES_ESP32S3_I2S`).
- **Glyph layer:** `GlyphMasks.h` is the one 9-bit mask table for both boards (46 glyphs; indices 0..36 frozen, append only). `LedGlyph` is `LedGlyphT<ActiveGlyphProfile>` (`DisplayProfile.h`): `SevenSegmentProfile` (V1 hand-written zig-zag tables, `mask & 0x7F`) or `NineSegmentProfile` (one per-module table + module offset). To add a character: `Glyph` enum + mask, then `python helpers/preview_glyphs.py`.
  - The 7-segment collapse keeps bit 3 (`CENTER`) and drops `MID_LEFT`/`MID_RIGHT`; never OR (renders `0` as `8`) or AND them.
  - Segment loops are bounded by `SegmentTable.count` (0 for V2's colon, 1 for indicators), **never** by a widest segment count.
  - Blink: `tickMs % 500 < 250` is the dark phase, shared by glyphs and border.

### Pinout
All pins live in `src/Board.h` (per `BOARD_REV`).

| Function | V1 GPIO | V2 GPIO |
|---|---|---|
| Remote A / B / C / D (prev / next / undo / enter) | 14 / 13 / 10 / 8 | **8 / 10 / 13 / 14** (reversed, verified on the device) |
| WS2812B data | 18 | 18 |
| Buzzer (active high) | 3 | 3 (via MOSFET) |
| OLED I2C SDA / SCL | 33 / 34 | 4 / 5 (33-37 are octal PSRAM on N16R8) |
| E-paper SCK / MOSI / CS / DC / RST / BUSY | - | 12 / 11 / 9 / 15 / 16 / 17 |
| Battery ADC (ADC1, 10k/10k divider) | - | 6 |

**V2 LED slots** (verified on the device 2026-09-17; several differ from the schematic-era notes):
- 0,1 border bottom-left; 2,3 top-left; **4 back indicator B**; 5,6 bottom-right; 7,8 top-right; **9 back indicator A**.
- Digit modules are chained in **reverse**: A (leftmost) 58-73, B 42-57, C 26-41, D 10-25. Per-module slot 2 is dead: 12, 28, 44, 60 are never written.
- On the bench, LEDs and buzzer need **battery power**; USB alone does not feed the 5 V rail.

### Input
- `RemoteInputManager` — manages 4 `RemoteInput` buttons (A/B/C/D) triggered by GPIO interrupts from the 433 MHz receiver. Use `button.takeActionIfPossible(debounceMs)` in views.
- The 433 MHz receiver generates multiple RISING edges per button press (RF noise). `RemoteInput::trigger()` must guard against this — do not remove the debounce check without understanding the double-trigger bug.
- `Buzzer` (`src/Buzzer.h`, GPIO 3) plays a short tone on remote presses and a victory theme on game win; toggled via `PrefsData.enableBuzzer`.
- **Never call `ESP.restart()` directly — use `safeRestart()` (`src/SafeRestart.h`).** Every pad returns to a floating input at reset and GPIO 3 has no default pull, so on V2 the MOSFET gate floats and the buzzer sounds through the reboot. A LOW written before the restart is discarded; `safeRestart()` latches the pad with `gpio_hold_en()`, which survives a *software* reset. `Buzzer::init()` releases it (`pinMode` → `LOW` → `gpio_hold_dis`, in that order — driving before unlatching leaves no floating gap) and runs as the **first** statement of `setup()`, before `Serial`/prefs/`initHardware()`. Crash, watchdog, brownout and power-on resets are not covered; only a hardware pull-down on the gate would fix those. An OTA *downgrade* to firmware without `gpio_hold_dis` leaves the buzzer muted until a power cycle.
- Powered by 1S2P INR18650-35E battery with 2A boost converter / charger.

### Persistence & Networking
- `PreferencesManager` — reads/writes `PrefsData` (WiFi SSID/password, brightness, AP mode) to ESP32 NVS.
- `RemoteDevelopmentService` — provides OTA firmware updates and WiFi-based serial logging.
- **This file is already Arduino-core-3.x-ready.** Two fixes were applied on
  2026-09-04 and are valid on *both* cores, so do not revert them if the platform
  is ever moved:
  1. `RemoteDevelopmentService.h` — `#include <WiFi.h>`. Core 2.0.17's
     `WebServer.h` transitively provided `WiFiServer`/`WiFiClient`; core 3.x pulls
     `NetworkServer`/`NetworkClient` instead. This one include fixes every such error.
  2. `RemoteDevelopmentService.cpp` — `WiFiClass::status()` became `WiFi.status()`.
     Calling a non-static member without an object was always invalid; GCC 8.4
     tolerated it, GCC 14 does not.
  With those in place the firmware builds clean on core 3.3.11 for **both**
  `lolin_s2_mini` and `esp32-s3-devkitc-1`. It is the LED driver, not the
  networking code, that blocks the upgrade.

### Players
Player profiles (`UserProfile`) are hardcoded in `main.cpp` with names and assigned colors. To add/change players, edit the `userA`–`userI` declarations and the `users` vector there.

### Battery (V2)
`BatterySensor` samples GPIO 6 at most every 200 ms into a rolling average (never block in `loop()`), with explicit 11 dB attenuation. Shown on the OLED Config screen and in the log. `FACTOR` (2.027) was calibrated against a meter on core 2.0.17.

### `lib/` directory
The `lib/` directory contains only backup files (`.h~`) and is not used for active code. All project source is under `src/`.
