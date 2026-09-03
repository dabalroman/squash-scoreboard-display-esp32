# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Firmware for a squash scoreboard display built on the Wemos S2 Mini (ESP32-S2). Written in C++ using PlatformIO and the Arduino framework. Supports squash, volleyball, and padel scoring with a front-facing 112× WS2812B LED display (4 digits + colon + indicators + a 24-LED history bar) and a rear OLED screen.

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

### 2. OTA is the only practical way to flash. USB needs disassembly.

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
pio run -e lolin_s2_mini                   # build
pio run -t upload -e lolin_s2_mini         # flash via USB (COM4) - needs disassembly
pio run -t upload -e lolin_s2_mini_ota     # flash via OTA (192.168.0.129) - normal path
pio device monitor                         # serial monitor (COM3, 115200)
```

Verify an OTA landed: `curl http://192.168.0.129/` should return the config page,
and port 23 is the telnet log. The device reboots after an upload and takes
~20-40 s to rejoin, so poll rather than assuming failure.

There is no test suite - this is embedded firmware with no automated testing.

**Toolchain gotcha, only relevant if this repo is ever moved off 6.13.0:** the
`pio` on PATH runs under Python 3.10, while `~/.platformio/penv` is Python 3.14.
The pioarduino platform's builder imports `littlefs`, whose compiled extension is
`cp314`, so PATH `pio` dies with `ImportError: cannot import name 'lfs'`. Use
`& "C:/Users/rd/.platformio/penv/Scripts/pio.exe"` in that case. **This does not
affect the current 6.13.0 setup** - PATH `pio` is verified working here.

A pre-build script (`helpers/version_increment.py`) auto-increments the firmware version on each build.

## Architecture

### Top-Level Loop (`src/main.cpp`)
Hardware is initialized in `setup()`. The main `loop()` runs at ~20fps (50ms tick). It polls `RemoteInputManager` for input and delegates to the active `DeviceMode`.

### DeviceMode + View pattern
- `DeviceMode` (abstract) — owns a state machine and an active `View`. Each mode has its own state enum (e.g., `SquashModeState`). When the state changes, a new `View` is instantiated.
- `View` (abstract) — each frame it calls `handleInput()`, `renderLedDisplay()` (front LEDs), and `renderBackDisplay()` (rear OLED). Flags `shouldRenderLedDisplay`/`shouldRenderBack` control dirty rendering.
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
- `ledDisplay` — wraps 112 WS2812B LEDs via FastLED. Exposes 4 digit glyphs (A–D), a colon, two player indicator LEDs, and a 24-pixel history bar (`LedBar`, starting at pixel index 88). Call `display()` to clear, render, and show in one step.
- `BackDisplay` — wraps the rear 0.96" OLED (Adafruit SSD1306 128×64). Provides helper methods like `renderScoreWidget()`.
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
  only forced when the ESP32-S3 display is built.
- `LedGlyph.h` has two separate tables: `SegmentToGlyphMap` (which segments light up per character) and `PixelsToSegmentMap` (which physical LED indices form each segment per digit position A–D). Adding a new character only requires a new entry in `SegmentToGlyphMap` + `Glyph` enum.

### Pinout
| GPIO | Function                          |
|------|-----------------------------------|
| 8    | Remote receiver input / interrupt |
| 10   | Remote receiver input / interrupt |
| 13   | Remote receiver input / interrupt |
| 14   | Remote receiver input / interrupt |
| 18   | WS2812B data                      |
| 3    | Buzzer (active high)              |
| 33   | I2C SDA (SSD1306)                 |
| 34   | I2C SCL (SSD1306)                 |

### Input
- `RemoteInputManager` — manages 4 `RemoteInput` buttons (A/B/C/D) triggered by GPIO interrupts from the 433 MHz receiver. Use `button.takeActionIfPossible(debounceMs)` in views.
- The 433 MHz receiver generates multiple RISING edges per button press (RF noise). `RemoteInput::trigger()` must guard against this — do not remove the debounce check without understanding the double-trigger bug.
- `Buzzer` (`src/Buzzer.h`, GPIO 3) plays a short tone on remote presses and a victory theme on game win; toggled via `PrefsData.enableBuzzer`.
- Powered by 18650 battery with boost converter / charger.

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

### Related work: the 9-segment display

A second, physically different display is in development — a 3D-printed
**9-segment** module. Its evaluation rig is a separate repo:
`../squash-scoreboard-display-testing-playground`, whose CLAUDE.md holds the full
backport plan. Key points for this repo:

- **Target board is the ESP32-S3 DevKitC-1**, not the S2 Mini. Warning: on S3
  modules with **octal PSRAM** (`R8` variants) GPIO 33-37 are reserved by the PSRAM
  interface, so a data pin carried over from the S2 may not exist there.
- The display is **4 modules + separator dots + extra LEDs**, wired identically in
  series, so digit N starts at `N * 16`. That means the per-digit pixel tables can
  be *generated* — the backport **deletes** the four hand-tabulated
  `glyphA`..`glyphD` tables rather than adding a fifth.
- **One shared mask table serves both displays.** The 9-segment table keeps bits
  0..6 identical to this repo's `SegmentToGlyphMap`, and
  `(nineSegmentMask & 0x7F)` reproduces the 7-segment mask exactly for all 37
  glyphs. Collapse the mid row by **keeping bit 3 (`CENTER`) and discarding
  `MID_LEFT`/`MID_RIGHT`** — do not OR the three together, which lights a middle
  bar on `0 1 7 C G L U I` and renders `0` as `8`.
- `Glyph` indices 0..36 are **frozen** and identical in both repos. Append only.
- The adapter seam is `LedGlyph`, below `LedDisplay`. Views, `Tournament`, `Match`,
  `Game` and `Rules` need no changes — they only ever speak `Glyph`, `Color` and
  blink flags.

### `lib/` directory
The `lib/` directory contains only backup files (`.h~`) and is not used for active code. All project source is under `src/`.
