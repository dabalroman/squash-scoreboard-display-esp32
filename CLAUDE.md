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
./run.sh --root ../../src --defines "-DBOARD_REV=1 -DLEDBAR_LAMBDA -DCONFIG_IDF_TARGET_ESP32S2=1 -DCURRENT_LEDDISPLAY_API"
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
Hardware is initialized in `setup()`. On V2 `LedSweepAnimation(pixels, bootParams()).play()` runs straight after `einkDisplay.begin()` — a blocking ~1 s LED boot sweep, deliberately placed **after** the e-paper's ~3 s init so it plays against the splash rather than a blank panel. `setup()` calls `einkDisplay.flushRefresh()` in between: the splash is only *requested* by `begin()` and would stay queued while the sweep blocks, since `loop()` has not started to pump `update()` yet. The main `loop()` runs at ~20fps (50ms tick). It polls `RemoteInputManager` for input and delegates to the active `DeviceMode`. `einkDisplay.update()` and `batterySensor.loop()` run on **every** pass, before the 50 ms gate (the e-paper polls its BUSY pin). No custom FreeRTOS tasks.

An `Overlay` (`src/Display/Overlay.h`) is the one thing that outranks the active mode: while `overlay.active()` it renders to all three displays and `deviceMode->loop()` is **skipped**, so the mode is *paused*, not changed — no input, no rendering, state and timers intact, and a pending score commit lands on the first frame after (the commit check lives in `handleInput`). On the pass it ends, `main.cpp` calls `RemoteInputManager::clearLatches()` (presses made during the overlay must not act on the view coming back), `Overlay::resetLedState()` and `DeviceMode::restoreView()`. The overlay knows nothing about modes, views or sports — it is content only (title, line, 4 glyphs, colour, duration), so #17's shutdown warning reuses it.

### DeviceMode + View pattern
- `DeviceMode` (abstract) — owns a state machine and an active `View`. Each mode has its own state enum (e.g., `SquashModeState`). When the state changes, a new `View` is instantiated. `activeView` lives in the **base class**, not in each mode, so `restoreView()` (re-run the view's three `init*Display` hooks + `queueRender()`) works without knowing which mode is active.
- `View` (abstract) — each frame it calls `handleInput()`, `renderLedDisplay()` (front LEDs), and `renderBackDisplay()` (rear OLED). Flags `shouldRenderLedDisplay`/`shouldRenderBack` control dirty rendering.
- `renderEInkDisplay(EInkDisplay&)` is the third, non-pure hook (default: blank). It is called every frame, but the e-paper must refresh only on real change: views pass **values** to `EInkDisplay` (`showMatchScore`, `showBlank`), which compares them with what is shown. Never rely on `queueRender()` for it (GamePlaying views never call it). Start each override with `if (!einkDisplay.available()) return;` so V1 computes nothing for its stub.
- View flow per sport mode: `TournamentChoosePlayers` → `MatchStartGame` → `GamePlaying` → `GameOver` → back to `MatchStartGame`.
- Modes: `ModeSwitchingMode` (menu), `ConfigMode`, `SquashMode`, `VolleyballMode`, `PadelMode`, `PlayerSetupMode` (V2 roster editor)
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
- All-square at `PadelRules::GEMS_PER_SET` (6) the set goes to a **tiebreak**: the same `PadelGemScorer` with its target moved from 4 to 7 (win by 2, uncapped), so the set ends 7-6. `checkWinner` therefore also wins on *any* score above `GEMS_PER_SET` - only a won tiebreak can produce one.
- The tiebreak mode is **derived**, never latched: `PadelRules::isTiebreakScore()` on the committed gem score, re-applied after every gem commit and every step-back. That is what lets undo walk back out of a tiebreak without a second state machine. Displays print plain numbers instead of the ladder and the e-paper label reads `TIE` while the rows stay on gems - the panel is match-level, a partial refresh per rally would burn the ghosting budget.
- No serve or change-of-ends tracking: the players deliberately skip ends changes, and the board has never known who serves.

### Display
- **`#if BOARD_REV` only in `src/Board.h` and hardware wrapper headers** (`DisplayProfile.h`, `LedDisplay.h`, `LedCentralScreenBorder.h`, `Animation/LedSweepAnimation.h`, `Animation/LedSlotPositions.h`, `EInk/EInkDisplay.h`, `EInk/PlayerSetupWebUi.h`, `EInk/Images/PlayerSetupQr.h`, `BatterySensor.h`). Never in views, modes, `Tournament`, `Match`, `Game`, `Rules`. Wrappers keep identical APIs on both boards (empty stubs on V1).
- `ledDisplay` — wraps the WS2812B chain (`Board::LED_COUNT`: 112 on V1, 74 on V2). Exposes 4 digit glyphs (A–D), a colon (no LEDs on V2), two player indicators, and on V1 a 24-pixel history bar (`LedBar`, from index 88). Call `display()` to clear, render, and show in one step.
- **V2 has no history bar.** `setLedBarState` takes a **lambda** (`[&] { return XBarRenderer::toLedBarPixels(...); }`), never pixels: an argument is evaluated even into an empty setter. `resetAnimations` (the old `resetHistoryBar`) clears the bar on V1 and stops the celebration on V2. Keep `LedBar::PIXEL_COUNT = 24` on both boards (`MatchResultBarRenderer` breaks at 0).
- **Sweep animations (V2)** — `Animation/LedSweepAnimation.h` is one ring animation with two parameterisations: `bootParams()` (rainbow by radius, one 1 s cycle) and `celebrationParams()` (the winner's colour, three 800 ms cycles). `play()` is only a blocking driver over the same `start`/`active`/`render` state machine, so boot and celebration share one code path; never call it from `loop()`.
  - Positions come from `Animation/LedSlotPositions.h`; the animation writes black to every live slot it is not lighting, and leaves `SKIP` slots (indicators 4/9, dead 12/28/44/60) untouched.
  - The celebration's origin is the **winner's half** (`setOriginToHalf`), not the panel centre, so the wave breaks from their side. That moves every slot's radius, so the sweep length is recomputed per origin — and the worst radial gap grows from 254 units to 595. The ring lights a slot within `band()` (394) of its radius, so it only clears a gap of G while `394 > G/2`; an origin further out than a half centroid needs a wider band or the strip blanks mid-sweep. `check_v2` sweeps both sides to hold this.
  - While it runs, `LedDisplay::render()` skips digits, colon and border entirely — the indicators still render, since they face the players and the sweep cannot reach them. Leaving the view (or an `Overlay` frame) calls `resetAnimations()` and ends it.
- **Border (V2)** — `LedCentralScreenBorder` is its own concept, independent of the back indicators: `LedDisplay::setBorderEnabled(bool)` and `setBorderAppearance(top, bottom, blinkTop, blinkBottom)`. The indicator methods never touch it. Colours are used **as passed**, never `sameSideMode`-redirected (border faces front; indicators face back). It is the legend for the e-paper rows.
  - Every LED view calls `setBorderEnabled` in `initLedDisplay` (no implicit default, it would leak across views): off in the menus (`ConfigView`, `ModeSwitchingView`, the three `*TournamentChoosePlayersView`s), on everywhere else with the same colours as the indicators.
- **E-paper (V2)** — `EInkDisplay` wraps `EInkAsync` (ported from the rig): non-blocking refresh state machine, ~10 ms SPI bursts, the ~0.5 s panel wait polled from `loop()`, requests coalesce. Driver class `GxEPD2_290_GDEY029T94` (GxEPD2 pinned 1.6.9); never the blocking `GxEPD2_BW` in the loop. Panel is mounted upside down → canvas rotation 2. Black is `INK`, white `PAPER` (`Adafruit_SSD1306.h` #defines `BLACK`/`WHITE`). Shows match-level score only; the top half is the left court player. **`BackDisplay` and the e-ink share nothing** (no base class, helpers or interface).
  - Match screen: **name, score, divider, score, name** down the panel, so each player's name is at their outer edge and the two scores face each other across the label. The label names the sport on `MatchStartGame` and what the numbers count elsewhere (`SETY` for squash/volleyball, `GEMY`/`TIEBREAK` for padel). Geometry is the `MATCH_*` enum in `EInkDisplay.h`.
  - **The big score has its own font.** `Fonts/ScoreDigits.h` is generated by `helpers/eink_font.py` from a TTF (Arial Bold, 108 ppem, digits only, 79 px cap height, ~4.9 KB PROGMEM). Never `setTextSize()` on it: scaling the 24 pt font by 3 is what made the digits stair-stepped, and the panel is 1-bit so there is no antialiasing to fall back on. The size is picked **by width** - two of the widest digits fit 128 px - so no score can overflow and there is no size-fallback branch.
  - Chrome lives in `EInkWidgets.h`: `drawHeader`, `drawFooter`, `drawTickbox`, `drawBatteryIcon`, the text helpers and `EInkLayout`. It sits inside `EInkDisplay.h`'s `BOARD_REV == 2` branch, so it needs no `#if` of its own and V1 never pulls in the GFX fonts.
  - Menus: views pass `EInkMenuRow`s to `showMenu()`; FreeSans 12 pt, selected row outlined (not filled), tickboxes for in/out. **One font size, always** - an overlong label is clipped, never shrunk to 9 pt; the automatic shrink made whole screens look ragged. `ROW_PITCH` is 32. The scroll window is each renderer's own state - `Scrollable` holds only options + selection + wrap (the OLED `ScrollableWidget` keeps its 3-row window, the e-paper its own).
  - Footer: `EInkFooter{line1, line2, line3, batteryPercent}`, up to three **centred** lines - a 12 pt headline (the battery reading when `batteryPercent >= 0`, else `line1`) and two 9 pt lines under it. One item per line: 128 px will not hold a 12 pt percentage beside a right-aligned version without them colliding. All three heights (31/48/62) keep `visibleRows()` at **6**, so a line appearing or disappearing never re-flows the menu - which is what lets the 6-entry MODE menu and the 5-row CONFIG menu both sit there without scrolling. A 7th MODE entry would start scrolling it.
  - The battery icon is an outline, a nub and a **three-segment gauge**: > 80 % three bars, > 50 % two, > 20 % one, at or below 20 % none, each bar drawn full or as an outline. Coarse on purpose - the percent beside it carries the exact value.
  - Ghosting: `EInkPolicy` - a screen-type change after >= 16 partials, or 128 partials in any case, becomes a non-blocking full refresh (~1.6 s flash, loop keeps running); every full refresh resets the counter.
  - Never write a raw NUL into a source file from a script (`'\\0'` in a Python string becomes a real NUL): the file still compiles but git treats it as binary.
  - Images: full-screen only. `helpers/eink_image.py <png>` converts a **128×296, pure black/white, pre-dithered** PNG (authored upright, no alpha) into a committed `PROGMEM` header under `src/Display/EInk/Images/`; the converter never thresholds or dithers, it rejects anything else. The generated header is committed so a build needs neither Python nor Pillow; the source PNG is **not** in git (`/assets/eink/` is ignored) - keep the artwork locally and rerun the script after changing it. The boot splash is one such image and holds the panel for `SPLASH_HOLD_MS` (4 s) — `show*` return early meanwhile — until `dismissSplash()`, which `main.cpp` calls on any remote press.
  - The firmware version is shown only in the CONFIG menu footer, `V`-prefixed on line 2 under the battery, with the IP on line 3. Never on the splash.
- `BackDisplay` — wraps the rear 0.96" OLED (Adafruit SSD1306 128×64). Provides helper methods like `renderScoreWidget()`. Rotation is `Board::OLED_ROTATION` (V1 2, V2 0).
  - **The V2 bench panel is damaged: every *even* pixel row from 0 to 12 is dead** (alternating COM lines, measured with a staircase ruler 2026-09-18) - not a solid strip, which is why a small shift looks like no change at all. `BackDisplay::DEAD_TOP_ROWS` (11) is the first row text may occupy; 13 would clear the damage completely, but at 11 only the stripe at row 12 crosses a glyph and one missing line is hard to notice. Set it to 0 to revert the whole workaround - no other edit. Global on both boards on purpose, no `BOARD_REV` split: it costs V1's healthy panel 10 px on the big font and that is not worth a second code path.
  - The clamp lives in `clearDeadTop()`, called from `print()`/`println()` - **the one point every OLED text draw passes through**, so no cursor setter can be missed. Anything bypassing it must clamp for itself: `Overlay::printBuiltIn` does (built-in font's cursor y is the glyph top, not a baseline), and it is the only such site left.
  - The drop comes from the **font's** ascent, measured once per `initBigFont`/`initSmallFont` from a sample string, never from the individual string. Fitting each string exactly makes the top score hop 1-2 px as its value changes (`1` and `4` ascend 28/27 against 29 for the rest) and puts the menu's `>` marker a pixel off its label. Line 0 big-font baseline is therefore a fixed 40, small-font 21.
  - **No battery readout on the OLED.** It sat wholly inside the dead rows and the 3-row menu leaves it nowhere to move, so it was removed for good - the e-paper already carries it on both screens that showed it. That removal is *not* part of the `DEAD_TOP_ROWS` revert.
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
  - `LedText::toWord()` (`LedText.h`) maps a string to the 4 digit glyphs, so LED words live in `Strings.h` as text. Case-sensitive where the table has both forms (C/c, H/h, I/i, L/l, U/u), case-folding where it has one; write the word as it lights up (`"buZZ"`, `"oPCJ"`). Unmapped letters render blank. Call sites use `LedDisplay::setGlyphsText()`.
  - The 7-segment collapse keeps bit 3 (`CENTER`) and drops `MID_LEFT`/`MID_RIGHT`; never OR (renders `0` as `8`) or AND them.
  - Segment loops are bounded by `SegmentTable.count` (0 for V2's colon, 1 for indicators), **never** by a widest segment count.
  - Blink: `tickMs % 500 < 250` is the dark phase, shared by glyphs and border.
  - **`digitToGlyph` returns `Empty` above 9**, so any slot fed a raw id blanks from profile 10 up. Both screens that show an id feed it `% 10`:
    - The three `*TournamentChoosePlayersView.h` render `P`, the ones digit, a blank, then the in/out dot - `P5 *` in (UpperDot, green), `P5 .` out (LowerDot, red). The dot's height carries the state on its own, not only the green/red tint that `setGlyphsColor(playerColor, playerStateColor)` gives slots C and D.
    - The three `*MatchStartGameView.h` render `P<id>P<id>`, two slots per player, so ids 5 and 15 read alike there; the per-player colours are what separate them.
  - `TournamentPlayersBarRenderer` clamps instead of blanking: from 13 selected players the segment width hit 0 and the whole V1 bar went dark, which a 32-profile roster makes easy to reach.

### Mode selector
One `ModeMenuEntry` table in `ModeSwitchingView.h` drives every row: OLED label, e-paper label, LED word, target `DeviceModeState`, colour and V1 bar slot. They used to be four hand-aligned lists plus an enum, which drifted; add a row, do not add a list. `enabled` hides a row at runtime (`Board::HAS_PLAYER_SETUP`) - never an `#if` here.

Member declaration order in that view is load-bearing: `entryIds` feeds `optionsList`, which `Scrollable` binds **by reference** and whose size it snapshots at construction. Both are `const` and never resized afterwards.

`ModeSwitchingBarRenderer` takes a slot index and a colour rather than the menu index, so reordering the menu cannot leave the V1 bar showing another sport's colour.

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
- Digit modules are chained in **reverse**: A (leftmost) 58-73, B 42-57, C 26-41, D 10-25. Per-module slot 2 is dead: 12, 28, 44, 60 are never written — it is a chain position on the right column with no die fitted, not a bottom-row LED.
- **Physical LED coordinates live in `assets/led-map.svg`** (vector, authoritative; `led-map.png` is a raster preview). `helpers/led_positions.py` flattens it into `Animation/LedSlotPositions.h`; `--check` verifies the committed table still matches. The ASCII drawing in `NineSegmentProfile.h` is schematic and **not to scale** — never derive geometry from it. Each module is 3x6 dies on a uniform 197-unit grid (~16 units/mm), and 5 slots per module drive 2 parallel dies.
- On the bench, LEDs and buzzer need **battery power**; USB alone does not feed the 5 V rail.

### Input
- `RemoteInputManager` — manages 4 `RemoteInput` buttons (A/B/C/D) triggered by GPIO interrupts from the 433 MHz receiver. Use `button.takeActionIfPossible(debounceMs)` in views.
- The 433 MHz receiver generates multiple RISING edges per button press (RF noise). `RemoteInput::trigger()` must guard against this — do not remove the debounce check without understanding the double-trigger bug.
- **Measured receiver behaviour (V2 bench probe, 2026-09-18, 37 presses).** The output is a **clean latch**: one contiguous HIGH per press, LOW on release, **zero dropouts**, idle driven LOW. Taps run **236-360 ms** (median 282); deliberate holds run 2498-7595 ms and scale linearly. Same part on both boards. This is why long-press detection is a **level poll** (`RemoteInput::poll()`, every loop pass) and not edge timing — do not reintroduce a gap/burst heuristic.
- **On the three `*TournamentChoosePlayersView`s, short C = back to the mode selector** and D alone toggles a player or presses START. That is exactly what a long press already did there, which is what let the `KONIEC` row go.
- **Long press on C = back one step** (`LONG_PRESS_MS = 2000`, one shot per press). `DeviceMode::goBack()` returns false when there is nowhere to go; `main.cpp` then stays silent, and that silence *is* the feedback. `GamePlaying` returns false as an **explicit case** so a hold can never discard a live game — it is a deliberate exception, not a missing case. Every fire is logged, so a dead detector is distinguishable from an intended no-op.
  - The threshold is 2000 ms because people undershoot their own count: holds aimed at "3 seconds" measured 2498-2951 ms, so a 3000 ms threshold missed 7 of 13. Longest tap was 360 ms, so the margin is still 5.5x. **Do not raise it back to a round 3000.**
  - **Button C alone fires its short action on release**, not on press (`setDeferToRelease`), costing ~280 ms — otherwise a hold performs undo/confirm on its way to the back. A/B/D stay instant on press, so scoring keeps its latency.
- `Buzzer` (`src/Buzzer.h`, GPIO 3) plays a short tone on remote presses and a victory theme on game win; toggled via `PrefsData.enableBuzzer`.
- **Never call `ESP.restart()` directly — use `safeRestart()` (`src/SafeRestart.h`).** Every pad returns to a floating input at reset and GPIO 3 has no default pull, so on V2 the MOSFET gate floats and the buzzer sounds through the reboot. A LOW written before the restart is discarded; `safeRestart()` latches the pad with `gpio_hold_en()`, which survives a *software* reset. `Buzzer::init()` releases it (`pinMode` → `LOW` → `gpio_hold_dis`, in that order — driving before unlatching leaves no floating gap) and runs as the **first** statement of `setup()`, before `Serial`/prefs/`initHardware()`. Crash, watchdog, brownout and power-on resets are not covered; only a hardware pull-down on the gate would fix those. An OTA *downgrade* to firmware without `gpio_hold_dis` leaves the buzzer muted until a power cycle.
- Powered by 1S2P INR18650-35E battery with 2A boost converter / charger.

### Persistence & Networking
- `PreferencesManager` — reads/writes `PrefsData` (WiFi SSID/password, brightness, AP mode) to ESP32 NVS. `wifiIpAddress` is **live state, never persisted**: empty until an interface comes up, set from `WiFi.localIP()` on an STA connect and `WiFi.softAPIP()` in both AP paths, cleared when WiFi is off or an AP drops. The CONFIG footer omits its line while it is empty, which is why it defaults to empty rather than to a placeholder.
- **`enableWifi` defaults to 0.** A device with empty or invalid NVS boots with STA, AP, OTA and telnet all off, instead of spending ~15 s failing STA against an empty SSID and then raising an open AP. Stored blobs keep their own value, so V1's OTA path is untouched. Recovery on a wiped device is CONFIG -> WiFi ON -> `[Reboot]`, from the remote alone.
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

### Strings (UI language)
Every user-visible string is a `constexpr const char* const` in `src/Strings.h`, chosen by `#if LANG_PL` / `#else`. **One language per build**: `-DLANG_PL` is in `build_flags` for both envs (`lolin_s2_mini_ota` inherits via `extends`), so the unselected branch never reaches the preprocessor. Never index a two-row table at runtime - that ships both languages.

- The device is Polish. The English table stays as the unbuilt `#else` branch for reference; **edit both sides** or the other language silently rots.
- **No diacritics anywhere.** Every GFX font declares range `0x20-0x7E` and `GlyphMasks.h` has no accented glyphs, so words that would need one were *replaced*, not stripped: `SIATKA` (not siatkowka), `NISKA` (not slaba).
- LED words are constrained further, to the 46-glyph table - **no W, K, M or V**. Return is `COFNIJ`, not `WSTECZ`, because the LEDs, OLED and e-paper are readable at once and must agree.
- Polish numerals decline (1 gracz / 2-4 gracze / 5+ graczy), so a `"%u <noun>"` format string is wrong for some counts. Use a label-colon form (`"W GRZE: %u"`).
- The `_OLED` variants exist because the OLED list is space-padded for centring at a fixed x (10 chars max, `FreeMono9pt7b`), while the e-paper rows are not.
- Out of scope: `printLn`/telnet/serial logs, the WiFi config web page, player names, the AP SSID/password, and the `BAT` / `FW` abbreviations.
- Verify a language change with `grep -ac "<english word>" .pio/build/esp32s3_devkitc/firmware.bin` - it must return 0.

### Players / profiles
The roster is **data, not code**: up to 32 profiles in NVS, edited from a phone (see *Roster editor*). The nine `FACTORY_PLAYERS` in `main.cpp` are only the fallback, used when NVS holds nothing valid and by "restore factory profiles".

- `PlayerRoster` (`src/PlayerRoster.h`, board-agnostic) owns the profiles and exposes `profiles()` as `std::vector<UserProfile *> &`, so every mode's signature is unchanged. Built exactly once, by `load()` in `setup()`.
- Its own NVS key `"ply"` in namespace `"ns"` - **never a field in `PrefsData`**, whose `read()` rejects the blob unless `getBytesLength == sizeof(PrefsData)`, so growing it would drop brightness *and* the WiFi credentials.
- Blob is fixed-size `PlayersData` v2. `readBlob()` recognises the v1 layout (no uid) by its length and migrates it in place rather than reseeding, so an update never wipes a roster. Keep that path when adding a v3.
- **Two identifiers, deliberately** (`UserProfile`): `id` is the position in this boot's roster - what the LEDs show as `P  3` and what `MatchOrderKeeper` keys on - and is renumbered by any reorder. `uid` is a `uint32_t` from `esp_random()`, stored beside the name, and survives rename/recolour/reorder. Use `id` inside a match, `uid` for anything outliving one. Nothing persists an `id`, which is what makes positional ids safe.
- Colours come from `PlayerPalette` / `PlayerColors` (`src/PlayerPalette.h`), **not** `Colors::` - those are UI accents. 16 entries, user-specified. Read the caveat block in that header before changing them: the set is screen-derived, so several pairs separate only by lightness, which a WS2812 conveys poorly.
- The web wire format is the palette **index**, never hex. `nearestIndex()` maps a stored colour that is no longer a preset onto the closest one, so a palette change does not silently recolour everyone.
- Duplicate names and duplicate colours are both allowed and unwarned; the `uid` is what makes two people called Krystian two people.

### Roster editor (V2)
`PlayerSetupMode` + `PlayerSetupView`, reached from the mode selector ("PROFILE"). Entering **forces the AP up** whatever `enableWifi` says (`RemoteDevelopmentService::enablePlayerSetupAp()`: tears STA down, no blocking delay); the editor never runs over the house network, so it is always reached by the placard's QR codes and never by a guessed IP. The e-paper shows a pre-rendered dual-QR placard (`EInkDisplay::showImage()`, always a full refresh - a ghosted QR will not scan). C or D exits, and it closes itself after 15 minutes idle.

- The AP is raised in the mode's **constructor** and dropped in its **destructor**, not in a button handler, so every exit path tears it down identically.
- `PlayerSetupWebUi` lives at **file scope in `main.cpp`**, because `WebServer` (core 2.0.17) has no `removeHandler`: a route handler outlives every mode, so no route lambda may capture a view. Requests arriving while the screen is shut are refused by an `active` gate, not by unregistering.
- Routes: `GET /` (editor), `POST /save`, `GET /update` (firmware screen; the WiFi credentials form lives there, since WiFi exists only to serve OTA). `POST /connect` and `POST /update` stay in `RemoteDevelopmentService`. The legacy `/` credentials form is registered **after** `extraRoutes` - WebServer dispatches to the first matching handler, so V2's root wins and V1, which has no web UI, still gets a form. That ordering is the V1 fallback; do not move it.
- The save is server-authoritative and atomic: every field validated on its own, whole blob staged in RAM, one `putBytes`, then a deferred `safeRestart()`. Any rejection is a 400 and leaves NVS untouched.
- The page must post `application/x-www-form-urlencoded`. Verified in the core's `Parsing.cpp`: `WEBSERVER_MAX_POST_ARGS` (32) caps only `_parseForm` (multipart); urlencoded bodies go to `_parseArguments`, which allocates per `&`.
- The page is **Polish-only and not in `Strings.h`** - it is rendered by a browser, so it carries real diacritics, unlike every GFX-font string. Profile *names* stay printable ASCII because those do reach the LED/OLED/e-paper fonts.
- The placard has **no title bar** - the two QR codes and their captions need the full 296 px, and the menu entry has just named the screen. It is a baked bitmap and cannot read `Strings.h`; its wording lives in `helpers/player_setup_qr.py`. Regenerate with `python helpers/player_setup_qr.py` (needs `pillow` and `qrcode`) after changing the AP name, password, URL or captions.

### Battery (V2)

### Battery (V2)
`BatterySensor` samples GPIO 6 at most every 200 ms into a rolling average (never block in `loop()`), with explicit 11 dB attenuation. `FACTOR` (2.027) was calibrated against a meter on core 2.0.17. Volts appear only in the log now; both screens show percent.

`BatteryMonitor` (`src/BatteryMonitor.h`) turns that voltage into what the user sees. Board-agnostic — **no `#if BOARD_REV`**; on V1 the sensor is unavailable, so nothing downstream fires.
- `voltsToPercent()` interpolates one curve: the **midpoint** of the resting-OCV and 10 W-load columns for the 1S2P INR18650-35E pack (3.340 V = 0 %, 4.175 V = 100 %, 10 % steps). It is a local `constexpr` inside the static method (a `static constexpr` array member is an ODR link error on GCC 8.4) and the single place to retune the mapping.
- Two separate numbers, deliberately: the **mapped** percent drives the thresholds, the **shown** percent (5 % steps, only ever falling, jumping up only on a >= 10 point rise) is what displays print — otherwise the readout flickers as LED load sags the cell.
- Low state: mapped < 30 % held for 10 s continuously; clears above 35 % (hysteresis), so a single LED-load sag does not trip it. `takeLowWarning()` is the one-shot that fires the overlay.
- The overlay is additionally rate-limited to one per `WARNING_COOLDOWN_MS` (5 min). Hysteresis alone is not enough: under LED load the voltage still floats across the 30/35 pair, so `low` clears and re-latches and the one-shot re-arms every time. The cooldown gates only the *warning* - `isLow()`, and therefore the brightness cap, keeps tracking the live state.
- While low, `main.cpp` sets `LedDisplay::setBrightnessCap(31)` (menu level 1). The cap is **not** persisted and callers never see it: `setBrightness()` stores what was requested and applies `min(requested, cap)`, so `ConfigView`'s brightness edits stay capped on their own. Always set brightness through `LedDisplay`, never `FastLED.setBrightness` directly.
- Shown on the e-paper only: the mode selector footer and the CONFIG menu footer. The rear OLED no longer prints it at all (see `BackDisplay`).

### `lib/` directory
The `lib/` directory contains only backup files (`.h~`) and is not used for active code. All project source is under `src/`.
