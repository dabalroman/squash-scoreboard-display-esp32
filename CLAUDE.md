# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Firmware for a squash scoreboard display built on the Wemos S2 Mini (ESP32-S2). Written in C++ using PlatformIO and the Arduino framework. Supports squash, volleyball, and padel scoring with a front-facing 112× WS2812B LED display (4 digits + colon + indicators + a 24-LED history bar) and a rear OLED screen.

## Build & Flash Commands

PlatformIO is installed on Windows. When building from WSL, invoke via `cmd.exe`:

```bash
# Build only (from WSL)
cmd.exe /c "cd /d C:\localhost\squash-scoreboard-display-esp32 && pio run -e lolin_s2_mini"

# Flash via USB (COM4; see platformio.ini)
cmd.exe /c "cd /d C:\localhost\squash-scoreboard-display-esp32 && pio run -t upload -e lolin_s2_mini"

# Flash via OTA (WiFi; upload_port IP set in platformio.ini)
cmd.exe /c "cd /d C:\localhost\squash-scoreboard-display-esp32 && pio run -t upload -e lolin_s2_mini_ota"

# Serial monitor (COM3, 115200 baud)
cmd.exe /c "cd /d C:\localhost\squash-scoreboard-display-esp32 && pio device monitor"
```

There is no test suite — this is embedded firmware with no automated testing.

From Windows PowerShell (this repo's default shell), `pio` is on PATH and the working directory is already the project root — run the `pio …` part directly. The `cmd.exe /c "cd /d … && …"` wrapper is only for WSL and errors in a POSIX shell (`cd: too many arguments`).

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

### Players
Player profiles (`UserProfile`) are hardcoded in `main.cpp` with names and assigned colors. To add/change players, edit the `userA`–`userI` declarations and the `users` vector there.

### `lib/` directory
The `lib/` directory contains only backup files (`.h~`) and is not used for active code. All project source is under `src/`.
