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

Cause: FastLED silently picks its RMT driver from the IDF version, with no way to
override it.

| | RMT4 (core 2.0.17, works) | RMT5 (core 3.x, breaks) |
|---|---|---|
| ISR owner | FastLED's own, tight, IRAM | generic IDF `rmt_tx` + encoder callback |
| Refill buffer | default `FASTLED_RMT_MEM_BLOCKS 2` = **128 symbols** (now 4, see below) | `mem_block_symbols = 0` -> default **64** |
| Refill slack (S3, 4 blocks) | half buffer; bail at **60 us** late, stale replay at **120 us** | half the buffer, heavier refill |

Half the buffer and a heavier refill path, against this board's four RF-receiver
GPIO ISRs (which fire in *bursts* from RF noise - see Input below) plus I2C.
Refills land late, the RMT starves mid-frame, and the WS2812 stream shifts, so
every downstream LED receives its neighbour's bytes.

On core 3.x, `-DFASTLED_RMT5=0` (FastLED's RMT4 path on IDF 5.x) is the first thing
#74 must try. RMT5 itself has no tuning knobs: `memory_block_symbols = with_dma ? 1024 : 0` is
  hardcoded in `rmt_5/strip_rmt.cpp`, and `.with_dma = false` is hardcoded too.

**FastLED is also pinned at 3.9.16.** Neither newer release builds here: 3.10.4
fails in `fl/gfx/crgb.h`, 3.10.3 fails on `fl::fl_map`, and installing 3.10.3 from
the registry crashes PlatformIO's library manager. The registry is stale at 3.10.3
anyway; upstream GitHub has 3.10.4.

**WiFi LED glitches (task #58; re-measure on V1 is task #72).** With WiFi on, 1-2 LEDs
can flash a wrong colour. WiFi runs on core 0, loop()/FastLED/the RMT ISR on core 1. Mechanism, from `rmt_4/idf4_rmt_impl.cpp`: the refill ISR has
one half-buffer of slack (`PULSES_PER_FILL x 1.25 us`). More than 50 % late ->
`fillNext` **bails** (frame cut short; the rest of the chain keeps the identical
previous frame, invisible). More than 100 % late -> the RMT **replays the stale half**
(8 bytes = 2.67 LEDs, channels rotate) - the visible glitch. Thresholds (bail / stale):
30 / 60 us at 2 blocks, 60 / 120 us at 4.

- The worst case is a fallback leaving **STA enabled**: the core's auto-reconnect re-runs
  `WiFi.begin()` after every NO_AP_FOUND, and each channel-hopping scan stalls the refill
  by hundreds of us. No RMT buffer covers that; only stopping STA does.
- **Fixed (task #64):** every AP path goes through `RemoteDevelopmentService::startApOnly()`
  (STA off, `WIFI_AP`, `softAP`), and `-DFASTLED_RMT_MEM_BLOCKS=4` is in both firmware
  envs' `build_flags`. It must stay a build flag - FastLED's own `.cpp` reads it.
  A fallback AP stays AP-only until reboot: no late STA connect, no periodic retry.
  Signal level (3.3 V data, no level shifter) is not the
  cause: glitches tracked the stale counter exactly.

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

- `v1_bootstrap` is the first-flash image of a **naked** V1 (empty NVS, no remote to reach
  Dev Mode): `helpers/wifi_bootstrap.py` reads `SCOREBOARD_WIFI_SSID` /
  `SCOREBOARD_WIFI_PASSWORD` from the environment (build fails if unset or over 63 bytes)
  into `.pio/build/v1_bootstrap/bootstrap/BootstrapWifi.generated.h`, on that env's include
  path only. `PreferencesManager::read()` seeds them with `enableDevMode = 1` **only when
  no valid blob exists** (`PrefsBootstrap::seed`, `test_prefs_bootstrap`). No other image
  contains the credentials. Delete that build dir afterwards; never upload it over OTA.
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

- The native envs use CLion's bundled MinGW: `test/tools/native_toolchain.py` picks the newest `C:/Program Files/JetBrains/CLion */bin/mingw/bin`, prints the g++ it chose and fails loudly if none exists. It replaces `version_increment.py`, so a test run never bumps `version.txt`. Tests include headers only; `src/*.cpp` is never compiled (a suite that needs one `#include`s it).
- Suites: `test_boot` (boot sweep byte-identical to `test/fixtures/golden_boot_v<n>.txt`), `test_v1_goldens` (V1 glyph + frame dumps vs the goldens taken before the V2 work), `test_v2_glyphs` (V2 slot invariants + brightness compensation), `test_rules`, `test_layers`, `test_sweep`, `test_intro` (both boards). Shared code in `test/common/`, the Arduino/FastLED host shim in `test/shim/`.
- A new suite is a `test/test_<name>/test_main.cpp` (runs on both boards); `test_v1_*` / `test_v2_*` run on one board only.
- Fixtures are `-text` in `.gitattributes`. A golden mismatch writes the actual output to `.pio/test-out/<env>/` and names the first differing line. Rebaseline only by copying that output over the fixture on purpose, with the reason in the commit; a mismatch caused by the toolchain is never rebaselined.
- **`[env]` sets `test_ignore = *` - never remove it.** Without it a bare `pio test` builds Unity images for the firmware envs and uploads them, to V1 over OTA included.

Logs: `printLn` goes to telnet (port 23) and, on both boards, to USB CDC (115200)
whenever a host is attached (`if (Serial)` in `LoggerHelper.h`).

**Toolchain gotcha, only relevant if this repo is ever moved off 6.13.0:** the
`pio` on PATH runs under Python 3.10, while `~/.platformio/penv` is Python 3.14.
The pioarduino platform's builder imports `littlefs`, whose compiled extension is
`cp314`, so PATH `pio` dies with `ImportError: cannot import name 'lfs'`. Use
`& "$HOME/.platformio/penv/Scripts/pio.exe"` in that case. **This does not
affect the current 6.13.0 setup** - PATH `pio` is verified working here.

A pre-build script (`helpers/version_increment.py`) auto-increments the firmware version on each build - once **per env**, so a two-env build bumps it twice. It tolerates a UTF-8 BOM in `version.txt` (a BOM once broke every build).

## Architecture

### Top-Level Loop (`src/main.cpp`)
Hardware is initialized in `setup()`. On both boards `ledDisplay.playBootSweep()` runs straight after `einkDisplay.begin()` (a stub on V1) — a blocking ~1 s LED boot sweep, deliberately placed **after** the e-paper's ~3 s init so it plays against the splash rather than a blank panel. `setup()` calls `einkDisplay.flushRefresh()` in between: the splash is only *requested* by `begin()` and would stay queued while the sweep blocks, since `loop()` has not started to pump `update()` yet. The main `loop()` runs at ~20fps (50ms tick). It polls `RemoteInputManager` for input and delegates to the active `DeviceMode`. `einkDisplay.update()` and `batterySensor.loop()` run on **every** pass, before the 50 ms gate (the e-paper polls its BUSY pin). No custom FreeRTOS tasks.

An `Overlay` (`src/Display/Overlay.h`) is the one thing that outranks the active mode: while `overlay.active()` it renders to all three displays and `deviceMode->loop()` is **skipped**, so the mode is *paused*, not changed — no input, no rendering, state and timers intact, and a pending score commit lands on the first frame after (the commit check lives in `handleInput`). On the pass it ends, `main.cpp` calls `RemoteInputManager::clearLatches()` (presses made during the overlay must not act on the view coming back), `Overlay::resetLedState()` and `DeviceMode::restoreView()`. The overlay knows nothing about modes, views or sports — it is content only (title, line, 4 glyphs, colour, duration), so #17's shutdown warning reuses it.

### DeviceMode + View pattern
- `DeviceMode` (abstract) — owns a state machine and an active `View`. Each mode has its own state enum (e.g., `SquashModeState`). When the state changes, a new `View` is instantiated. `activeView` lives in the **base class**, not in each mode, so `restoreView()` (re-run the view's three `init*Display` hooks + `queueRender()`) works without knowing which mode is active.
- `View` (abstract) — each frame it calls `handleInput()`, `renderLedDisplay()` (front LEDs), and `renderBackDisplay()` (rear OLED). Flags `shouldRenderLedDisplay`/`shouldRenderBack` control dirty rendering.
- `renderEInkDisplay(EInkDisplay&)` is the third, non-pure hook (default: blank). It is called every frame, but the e-paper must refresh only on real change: views pass **values** to `EInkDisplay` (`showMatchScore`, `showBlank`), which compares them with what is shown. Never rely on `queueRender()` for it (GamePlaying views never call it). Start each override with `if (!einkDisplay.available()) return;` so V1 computes nothing for its stub.
- View flow per sport mode: `TournamentChoosePlayers` → `MatchStartGame` → `MatchIntro` → `GamePlaying` → `GameCelebration` → `GameOver` → back to `MatchStartGame`. Padel reaches `MatchIntro` once per set (never per gem), purely through its flow.
  - `MatchIntro` and `GameCelebration` are one template view each, shared by the three sports (`DeviceMode/Intro/MatchIntroView.h`, `DeviceMode/Celebration/GameCelebrationView.h`; copies of one shape, no shared base). Each ends when its animation reports inactive, on a skip (intro: any button; celebration: C/D, with `preventTriggerForMs()`), or on a second `initLedDisplay` (`restoreView()` after an Overlay - no replay). Long C goes back to `MatchStartGame` from both. A view that ends on its own timer must hand over **without** drawing its last frame: an expired layer leaves only the base, a visible black frame before the next view.
  - Their OLED and e-paper show exactly what the neighbouring view shows (intro: GamePlaying's first frame; celebration: the summary), so the e-paper refreshes on entry and not at the hand-over.
  - `GameCelebration` hosts every end-of-game variant: `CelebrationVariant.h`'s `selectCelebrationVariant()` returns `Bajgiel` when either score in the `GameResult` is 0 (squash 11-0, volleyball to 0, a padel set 6-0; a tiebreak set never is), else `Normal` (the sweep). Sport-agnostic, `Rules` untouched.
  - **Bajgiel**: `LedDisplay::startBajgiel(loserOnLeft)` puts `Animation/LedBajgielAnimation.h` on layer 2 as a grey comet (`Multiply` + `LitOnly` on the loser's score, no colour of its own - the base glyph is already the loser's colour): both 0s spin clockwise in phase for 5 s (1 s per turn, half-turn tail). Each slot's angle is computed once in `start()` around its digit's **bounding-box** centre (V2's dead slots would bias a mean); the render is integer-only. When it expires the base is the summary's frame. `celebrationActive()` / `stopCelebration()` cover both variants. The OLED shows `BAJGIELOLED_BITMAP`, the e-paper `BAJGIEL_EINK_BITMAP` (`EInk/Images/Bajgiel.h`, `nullptr` on V1 like `PlayerSetupQr.h`), then the summary repaints both.
  - `GameCelebrationView` checks `celebrationActive()` **before** `display()`, so a view ending on its own timer never draws a last frame.
  - Each sport mode's `handleStateChange()` starts with `remoteInputManager.clearLatches()`: the timer-driven hand-overs call no `preventTriggerForMs()`, and a press latched in between would otherwise act on the new view's first frame (skipping the summary, or scoring a point on GamePlaying).
- Modes: `ModeSwitchingMode` (menu), `ConfigMode`, `SquashMode`, `VolleyballMode`, `PadelMode`, `PlayerSetupMode` (roster editor)
- Mode transitions happen via a callback `onDeviceModeChange` passed down from `main.cpp`. It only **requests** the change (`requestDeviceMode`): the call arrives from `handleInput()`, inside the outgoing mode's own `loop()`, so constructing there would free `this` and the active view under the three render calls that still follow. `loop()` applies the swap at one point - after the `overlay.active()` gate (an overlay owns all three displays, so the incoming constructor must not draw underneath it) and before `overlay.takeFinished()`, so `restoreView()` lands on the new mode. It clears the flag, then `deviceMode.reset()`, then `buildDeviceMode()` - teardown before construction, which is what gets `PlayerSetupMode`'s AP down first. `setup()` calls `buildDeviceMode` directly.
- Views that need per-tick updates (blinking) must NOT guard `renderLedDisplay` with `if (!shouldRenderLedDisplay)` — that flag prevents every-tick rendering. Only use the guard for purely event-driven views.
- Always use `match->getLeftCourtSidePlayer()` / `getRightCourtSidePlayer()` in views for display positioning. Never use `getPlayerA()` / `getPlayerB()` directly — those ignore the court-side swap state.

### Match / Tournament / Rules
- All tournament/match/game code lives under `src/Tournament/`.
- `Tournament` owns a collection of `Match`es and tracks the active one. It also holds `MatchOrderKeeper` for round-robin scheduling.
- `Match` owns `Game`s. Each `Game` holds per-game scores and delegates win detection to a `Rules` instance.
- `GameResult` stores slim post-game results. `winnerPlayerId` is a `uint8_t` player ID (not `GameSide`) — resolved at write-time in `Match::finishGame()` respecting swap state.
- `Rules` (abstract) — `checkWinner(scoreA, scoreB)` returns `GameSide`. Implementations: `SquashRules`, `VolleyballRules`, `ShortVolleyballRules`, `PadelRules`. `Rules` also exposes `historyReserve()` so each sport pre-sizes its `GameScoreHistory` (Squash 32 default, Volleyball 64, Padel 16).
- **Game ball** - `Rules::willWinOnNextPointScored(a, b, side)` (non-pure, no overrides) = not already won and `checkWinner` with that side +1 returns that side. Per side (bool), never a returned `GameSide`. `Game::willWinOnNextPointScored(side)` applies it to the **committed** score: an uncommitted point neither starts nor stops it (40:00 then an uncommitted 40:15 keeps the left breathing).
- **On fire** - `Rules::onFireStreak()` (5; `PadelRules` 3, counting gems). `GameScoreHistory::committedStreak(side)` walks the history backwards: skips uncommitted `scored`, counts `lost` as committed (only `commit()` erases it, like `getRealScore()`), stops at the other side. `Game::isOnFire(side)` = streak >= threshold; per `Game`, never carried across games or sets.
- **Comeback** - `Game::commit()` sets a one-shot `takeComebackSide()` when a side scores (positive delta) while the opponent's committed streak **before** the commit is >= `onFireStreak()`: the point that breaks an on-fire streak, not a score gap. One per commit, none on a winning commit. Squash and volleyball views take it; padel's never does.
- Score changes are "uncommitted" until 4 seconds of inactivity (each GamePlaying view's local `COMMIT_TIMEOUT_MS = 4000`), then `game->commit()` is called.

### Padel scoring (two-level history)
- Padel maps its three-level structure onto the two-level engine without touching the shared model: the engine `Game` **is a set** (its score = gems won, its history entries = gems), and a separate `PadelGemScorer` (`src/Tournament/Game/`) tracks the within-gem 0/15/30/40/Ad ladder **below** the `Rules` abstraction.
- `PadelGemScorer` is backed by its own `GameScoreHistory` (rallies), mirroring how `Game` uses one. So there are two independent history levels: gems (on the engine `Game`, shown on the LED bar) and rallies (in the scorer).
- `PadelGamePlayingView` keeps a stack of finished gems' rally histories so undo can step back across gem boundaries. When a gem completes it hand-couples the levels: `game->scorePoint(gemWinner)` + snapshot the gem; step-back reverses both.
- All-square at `PadelRules::GEMS_PER_SET` (6) the set goes to a **tiebreak**: the same `PadelGemScorer` with its target moved from 4 to 7 (win by 2, uncapped), so the set ends 7-6. `checkWinner` therefore also wins on *any* score above `GEMS_PER_SET` - only a won tiebreak can produce one.
- The tiebreak mode is **derived**, never latched: `PadelRules::isTiebreakScore()` on the committed gem score, re-applied after every gem commit and every step-back. That is what lets undo walk back out of a tiebreak without a second state machine. Displays print plain numbers instead of the ladder (zero-padded on the LEDs, like every sport's `00:00`, and Love is `00` too) and the e-paper label reads `TIE` while the rows stay on gems - the panel is match-level, a partial refresh per rally would burn the ghosting budget.
- Padel breathes at **gem ball**: `PadelGemScorer::willWinOnNextRally(side)` on the committed rallies (40:00, 40:30, Ad; in the tiebreak the next rally wins the set). There is deliberately no set-ball breathing at gem level (5-4): it could breathe both sides at once.
- No serve or change-of-ends tracking: the players deliberately skip ends changes, and the board has never known who serves.

### Display
- **`#if BOARD_REV` only in `src/Board.h` and hardware wrapper headers** (`DisplayProfile.h`, `LedDisplay.h`, `LedCentralScreenBorder.h`, `Animation/LedSweepAnimation.h`, `Animation/LedSlotPositions.h`, `EInk/EInkDisplay.h`, `EInk/Images/PlayerSetupQr.h`, `EInk/Images/Bajgiel.h`, `BatterySensor.h`). Never in views, modes, `Tournament`, `Match`, `Game`, `Rules`. Wrappers keep identical APIs on both boards (empty stubs on V1).
- `ledDisplay` — wraps the WS2812B chain (`Board::LED_COUNT`: 112 on V1, 74 on V2). Exposes 4 digit glyphs (A–D), a colon (no LEDs on V2), two player indicators, and on V1 a 24-pixel history bar (`LedBar`, from index 88). Call `display()` to clear, render, and show in one step.
- **V2 has no history bar.** `setLedBarState` takes a **lambda** (`[&] { return XBarRenderer::toLedBarPixels(...); }`), never pixels: an argument is evaluated even into an empty setter. `resetAnimations` (the old `resetHistoryBar`) stops every layer animation (celebration, intro, breathing) on both boards and also clears the bar on V1. Keep `LedBar::PIXEL_COUNT = 24` on both boards (`MatchResultBarRenderer` breaks at 0).
- **Sweep animations (both boards)** — `Animation/LedSweepAnimation.h` is one ring animation with two parameterisations: `bootParams()` (rainbow by radius, one 1 s cycle) and `celebrationParams()` (the winner's colour, three 800 ms cycles). It is an `LedAnimation` on the layer engine (below) and writes only into the layer buffer, never `pixels[]`. `LedDisplay::playBootSweep()` is the blocking boot driver over `renderBootFrame()` - `setup()` only, never from `loop()`.
  - Positions come from `Animation/LedSlotPositions.h`; the animation lights only the ring and leaves the rest at the layer's identity (black), and never reaches `SKIP` slots (indicators 4/9, dead 12/28/44/60).
  - The celebration's origin is the **winner's half** (`setOriginToHalf`), not the panel centre, so the wave breaks from their side. That moves every slot's radius, so the sweep length is recomputed per origin — and the worst radial gap grows from 254 units to 595. The ring lights a slot within `Params::band` of its radius, so it only clears a gap of G while `band > G/2` (any band above 298 is safe); a thinner band, or an origin further out than a half centroid, blanks the strip mid-sweep. Both boot and celebration use **690** (~3.5 die pitches, was 394) - a look choice made on V1 so the ring washes over the score it passes. The width stays a per-params field so the animation remains configurable. `test_sweep` sweeps both sides to hold this.
  - The celebration is **layered, not a takeover**: layer 2, target `Front`, `AllSlots`, so score, colon, border and the V1 bar stay readable under it; the indicators are outside `Front`. It runs in `GameCelebrationView` (2.4 s, then the summary); the `GameOver` summary is static and calls `stopCelebration()` (layer 2 only) + `setBreathing(0)`, **not** `resetAnimations()`, which would also clear V1's history bar the summary still shows. An `Overlay` frame calls `resetAnimations()`.
  - Boot is the sweep `Screen`-blended over a black base, which is exactly the sweep alone: `test_boot` holds it byte-identical to `test/fixtures/golden_boot_v<n>.txt` (no regenerate switch; rebaselining is a manual copy). They were re-baselined once, for the 394 -> 690 band, after proving the layered boot equals the pre-layer sweep with only that value changed; any other re-baseline needs the same proof.
  - **V1** shares the class unmodified: boot origin is the colon midpoint, celebration a half centroid. V1's `SKIP` is only the back indicators 2/3 (no dead slots), so its history bar is swept too; `LedBar` lost its own celebration pulse (`LedBarMode.h` is gone) and only renders caller-supplied state. `setOriginToHalf` puts `x == 0` in **neither** half - V1's colon dies sit exactly there. The half test is `LedSlots::halfOf(x)` (`Left` / `Right` / `Seam`, in `LedSlotPositions.h` outside the generated tables), shared by the sweep and the intro - never a second copy. Both maps share the 197-unit die pitch, so `band` is **shared by both boards** - do not split it per board. Worst radial gaps: V2 254 / 595, V1 354 (colon) / 189 (half centroid). `test_sweep` guards this on both boards.
- **Layer engine** (`LedDisplay/Layers/`, board-agnostic) — `render()` draws the base (digits, colon, indicators, border, V1 bar), then `LedLayerStack::compose()` blends up to two `LedAnimation` layers onto `pixels[]`, before `showCompensated()`. CPU-only: it never touches FastLED or the RMT, so it cannot cause the READ FIRST corruption.
  - Each layer = animation + `BlendMode` (`Normal`, `Screen`, `Add`, `Lighten`, `Multiply`; 8-bit integer, `LedBlend.h`) + `LedTarget` element bits + `LayerMask` (`AllSlots`, or `LitOnly` = slots the base lit, captured once before layer 1). Black is the transparent layer colour, white for `Multiply` (`identityFor`).
  - Target -> slot comes from `elementMap`, which `LedDisplay` builds from each component's own `markSlots()` (glyph segment tables bounded by `count`, border, V1 bar) - never a hand-kept slot list. `Front` is everything but the indicators.
  - `BarLeft`/`BarRight` (bits 10/11) mark which side scored each V1 bar pixel. They are **not** in `Front` (they only sit on `Bar` slots, which `Front` covers), so celebration and boot masks are unchanged. `LedBarPixel.side` carries the owner (only `GameScoreHistoryBarRenderer` sets it); every bar state goes through `LedDisplay::applyBarState()`, whose `LedBar::markOwners()` clears both bits on all 24 slots, then re-marks, so no stale owner survives a new state or `resetAnimations()`' `{}`.
  - Layer 1 = game-ball breathing (GamePlaying). Layer 2 = the celebration or bajgiel (GameCelebration), the match intro (MatchIntro), or during GamePlaying the on-fire smoke or the comeback burst - never two at once.
  - **On-fire smoke** (`Layers/LedSmokeAnimation.h`): grey 2-octave value noise sampled at `LedSlots::POS`, scrolled toward -y so wisps rise (`CELL_UNITS` 887 ~ 4.5 die pitches, `MS_PER_CELL` 1350, `PEAK` 150; the fine octave moves at the same world speed - faster, or smaller cells, read as flicker, not scroll). `Screen` onto that side's lit digits **and its own V1 bar pixels** (`onFireTargets` = `LeftScore|BarLeft` / `RightScore|BarRight`, never the border), `LitOnly`. Lattice coordinates are precomputed; the per-frame path is integer only. `setSmokeBlend()` is the knob, unused in production. `LedDisplay::setOnFire(targets)` is the idempotent per-frame choke point; 0 clears layer 2 only when the smoke owns it.
  - **Game ball outranks on fire**: views compute breathing first and pass `isOnFire && !breathe` to `onFireTargets`, so a side at game ball breathes unmasked and carries no smoke.
  - **Comeback burst**: `LedDisplay::startComeback(color, onLeft)` reuses the `sweep` member with `comebackParams()` (one 800 ms cycle, band 690 - the shared band rule), origin on the scorer's half, layer 2 `Normal`/`Front`/`AllSlots`. It stops the smoke; `setOnFire()` yields while `comebackOwnsLayer2()` and retakes the layer when the burst ends. Views take the comeback side and start it **after** `setOnFire()`/`setBreathing()`, so a burst started this frame is not overwritten. `startCelebration`/`startIntro`/`startBajgiel`/`stopLayers` all stop the smoke.
  - Animation instances are `LedDisplay` members (no heap). `LedBreathingAnimation` (grey level, `Multiply`, one `cosf` per frame, 1400 ms period, level 102-255) is the **game-ball breathing**: `LedDisplay::setBreathing(uint16_t targets)` is an idempotent per-frame choke point - 0 stops it, the phase restarts only on off -> on, a repeat call only moves the mask. `breathingTargets(left, right)` maps a side to `LeftScore|BorderTop|BarLeft` / `RightScore|BorderBottom|BarRight`; the three GamePlaying views call it every frame before `display()`.
  - **Match intro** - `Animation/LedIntroAnimation.h`, an inward fill from the panel's outer edge to its centre (1000 ms wipe, 200 ms hold, soft edge 197 = one die pitch): left half in the left court player's colour, right half the right's, the `Seam` dark, `SKIP` never written. It is a fill, not a ring, so the sweep's band/radial-gap rule does not apply. Every element ends in its GamePlaying 0:0 state: V2's border top/bottom take left/right by element (its segments straddle the seam), and V1's bar - empty at 0:0 - fades out over a 400 ms out phase, front moving from the bar's nearest pixel outward (zero-length where nothing is outgoing, so V2 is 1.2 s). Both come from `elementMap` bits passed to `start()`, never a slot list. Layer 2, `Normal`, `Front`, `AllSlots`, over a dark base (glyphs empty, colon and border off, indicators steady in player colours).
  - The celebration blend is **Normal**, chosen on V1 against Screen/Add/Lighten; `setCelebrationBlend()` stays as the knob, but nothing in production calls it.
  - Glyph/border blink is **not** a layer: `LitOnly` simply sees the dark phase as unlit.
  - `test_layers` checks blend identities exhaustively, element maps, compose, breathing, bar owners and game-ball targets; `test_rules` the game-ball rules; `test_intro` the intro (SKIP/seam, monotone fill, side colours, border override, bar out phase).
- **Border (V2)** — `LedCentralScreenBorder` is its own concept, independent of the back indicators: `LedDisplay::setBorderEnabled(bool)` and `setBorderAppearance(top, bottom, blinkTop, blinkBottom)`. The indicator methods never touch it. Colours are used **as passed**, never `sameSideMode`-redirected (border faces front; indicators face back). It is the legend for the e-paper rows.
  - Every LED view calls `setBorderEnabled` in `initLedDisplay` (no implicit default, it would leak across views): off in the menus (`ConfigView`, `ModeSwitchingView`, the three `*TournamentChoosePlayersView`s), on everywhere else with the same colours as the indicators.
- **Per-segment brightness compensation (V2).** A nine-segment module's segments differ ~2.3x in lit area per LED, so equal PWM made the small ones glare. One choke point: `showCompensated(pixels)` in `DisplayProfile.h` = `ActiveGlyphProfile::compensate(pixels)` then `FastLED.show()`. `LedDisplay::display()` and both shows in `LedSweepAnimation::play()` go through it - **never call `FastLED.show()` from a render path**; only `main.cpp`'s all-black init frame does. V1's `SevenSegmentProfile::compensate` is an empty no-op, so V1 output is unchanged; V2 delegates to `Profiles/NineSegmentBrightness.h`, which scales `pixels[]` in place (safe: every frame rewrites every live slot, so nothing compounds). Measured per segment (dies / **total** mm², per die = total / dies): bottom 3/1103, bottom-left 2/374, bottom-right 2/766, mid 2/349, mid-left 2/331, mid-right 2/331, top 3/1044, top-left 2/345, top-right 2/693; border segment 2/473, every border segment identical. **The left/right asymmetry is physical and expected - do not "fix" it.** Scale is `min(1.0, 1.2 * mm2PerDie / 383.0)`: 1.2 is the accepted **20 % spread** in light per mm², and 383.0 (bottom-right, 766/2) is the largest area per die - it has to be the reference because the compensation can only attenuate. That gives 255/149/255/139/132/132/255/138/255 and border 189, stored as literals; indicators 4/9 and dead slots stay 255. `test_v2_glyphs` recomputes every factor from the areas and bounds the power. It replaces the old flat `brightness * 0.8f` as V2's power guard: `Board::GLOBAL_BRIGHTNESS_SCALE` is **1.0 on V2 and 0.8 on V1 permanently** (a power-draw limit, not aesthetics). Worst case (four `8`s + border + indicators, top level) is 69.6 die-equivalents against 72 under the old limit, so peak draw does not rise. Tune only `TOLERANCE`, and change the literals and `test_v2_glyphs` together.
- **E-paper (V2)** — `EInkDisplay` wraps `EInkAsync` (ported from the rig): non-blocking refresh state machine, ~10 ms SPI bursts, the ~0.5 s panel wait polled from `loop()`, requests coalesce. Driver class `GxEPD2_290_GDEY029T94` (GxEPD2 pinned 1.6.9); never the blocking `GxEPD2_BW` in the loop. Panel is mounted upside down → canvas rotation 2. Black is `INK`, white `PAPER` (`Adafruit_SSD1306.h` #defines `BLACK`/`WHITE`). Shows match-level score only; the top half is the left court player. **`BackDisplay` and the e-ink share nothing** (no base class, helpers or interface).
  - Match screen: **name, score, divider, score, name** down the panel, so each player's name is at their outer edge and the two scores face each other across the label. The label names the sport on `MatchStartGame` and what the numbers count elsewhere (`SETY` for squash/volleyball, `GEMY`/`TIEBREAK` for padel). Geometry is the `MATCH_*` enum in `EInkDisplay.h`.
  - **The big score has its own font.** `Fonts/ScoreDigits.h` is generated by `helpers/eink_font.py` from a TTF (Arial Bold, 108 ppem, digits only, 79 px cap height, ~4.9 KB PROGMEM). Never `setTextSize()` on it: scaling the 24 pt font by 3 is what made the digits stair-stepped, and the panel is 1-bit so there is no antialiasing to fall back on. The size is picked **by width** - two of the widest digits fit 128 px - so no score can overflow and there is no size-fallback branch.
  - Chrome lives in `EInkWidgets.h`: `drawHeader`, `drawFooter`, `drawTickbox`, `drawBatteryIcon`, the text helpers and `EInkLayout`. It sits inside `EInkDisplay.h`'s `BOARD_REV == 2` branch, so it needs no `#if` of its own and V1 never pulls in the GFX fonts.
  - Menus: views pass `EInkMenuRow`s to `showMenu()`; FreeSans 12 pt, selected row outlined (not filled), tickboxes for in/out (`EInkMenuRow.check` -1 none, 0 empty, 1 filled, 2 an "M" in the built-in 5x7 font - the buzzer's in-match state; `BUZZER` plus a value text does not fit 128 px). **One font size, always** - an overlong label is clipped, never shrunk to 9 pt; the automatic shrink made whole screens look ragged. `ROW_PITCH` is 32. The scroll window is each renderer's own state - `Scrollable` holds only options + selection + wrap (the OLED `ScrollableWidget` keeps its 3-row window, the e-paper its own).
  - Footer: `EInkFooter{line1, line2, line3, batteryPercent}`, up to three **centred** lines - a 12 pt headline (the battery reading when `batteryPercent >= 0`, else `line1`) and two 9 pt lines under it. One item per line: 128 px will not hold a 12 pt percentage beside a right-aligned version without them colliding. All three heights (31/48/62) keep `visibleRows()` at **6**, so a line appearing or disappearing never re-flows the menu - which is what lets the 6-entry MODE menu and the 5-row CONFIG menu both sit there without scrolling. A 7th MODE entry would start scrolling it.
  - The battery icon is an outline, a nub and a **three-segment gauge**: > 80 % three bars, > 50 % two, > 20 % one, at or below 20 % none, each bar drawn full or as an outline. Coarse on purpose - the percent beside it carries the exact value.
  - Ghosting: `EInkPolicy` - a screen-type change after >= 16 partials, or 128 partials in any case, becomes a non-blocking full refresh (~1.6 s flash, loop keeps running); every full refresh resets the counter.
  - Never write a raw NUL into a source file from a script (`'\\0'` in a Python string becomes a real NUL): the file still compiles but git treats it as binary.
  - Images: full-screen only. Greyscale artwork goes through `helpers/eink_dither.py <png>` first (flatten on white, autocontrast 1 %, Floyd-Steinberg -> `<name>_dither.png`; it reproduces the splash's dither exactly). `helpers/eink_image.py [--oled] <png> [out]` converts a **128×296, pure black/white, pre-dithered** PNG (authored upright, no alpha) into a committed `PROGMEM` header under `src/Display/EInk/Images/`; the converter never thresholds or dithers, it rejects anything else. The generated header is committed so a build needs neither Python nor Pillow; the source art is in git under `assets/eink/` (bajgiel `.xcf`/`.png`/`_dither.png`, the QR placard PNG) except the splash (`splash-text*` is ignored and stays local) - rerun the scripts after changing any of it. The boot splash is one such image and holds the panel for `SPLASH_HOLD_MS` (4 s) — `show*` return early meanwhile — until `dismissSplash()`, which `main.cpp` calls on any remote press.
  - The firmware version is shown only in the CONFIG menu footer, `V`-prefixed on line 2 under the battery, with the IP on line 3. Never on the splash.
- `BackDisplay` — wraps the rear 0.96" OLED (Adafruit SSD1306 128×64). Provides helper methods like `renderScoreWidget()`. Rotation is `Board::OLED_ROTATION` (V1 2, V2 0).
  - **The V2 bench panel is damaged: every *even* pixel row from 0 to 12 is dead** (alternating COM lines, measured with a staircase ruler 2026-09-18) - not a solid strip, which is why a small shift looks like no change at all. `BackDisplay::DEAD_TOP_ROWS` is the first row text may occupy and reads `Board::OLED_DEAD_TOP_ROWS` (V2 11, V1 0 - so `BackDisplay` carries no `#if`); 13 would clear V2's damage completely, but at 11 only the stripe at row 12 crosses a glyph and one missing line is hard to notice. Set V2's to 0 to revert the whole workaround - no other edit. It was global until V1 needed its healthy top strip for the battery readout (user's call, 2026-09-29).
  - The clamp lives in `clearDeadTop()`, called from `print()`/`println()` - **the one point every OLED text draw passes through**, so no cursor setter can be missed. Anything bypassing it must clamp for itself: `Overlay::printBuiltIn` does (built-in font's cursor y is the glyph top, not a baseline). `BackDisplay::drawBitmap()` (full-screen 128x64 art) deliberately does **not** clamp: artwork uses the whole panel (user's call, 2026-09-28).
  - The drop comes from the **font's** ascent, measured once per `initBigFont`/`initSmallFont` from a sample string, never from the individual string. Fitting each string exactly makes the top score hop 1-2 px as its value changes (`1` and `4` ascend 28/27 against 29 for the rest) and puts the menu's `>` marker a pixel off its label. Line 0 big-font baseline is therefore a fixed 40, small-font 21.
  - **Battery on the OLED: V1 only.** The mode selector and CONFIG call `BackDisplay::drawBatteryPercent` (built-in 5x7 font, right-aligned, y 1..7, above the menu's first row at y 10), gated on `!EInkDisplay::available() && batteryMonitor.available()` - never `BOARD_REV`. V2's percent lives on the e-paper; its OLED prints none (it sat inside the dead rows). The readout refreshes only when the view re-renders (input, `restoreView`).
- Bar renderers live in `src/Display/LedDisplay/Renderer/`. Each exposes a static `toLedBarPixels()` returning `std::array<LedBarPixel, LedBar::PIXEL_COUNT>`.
- **The language standard is C++11** (`-std=gnu++11`, set by the pinned Arduino core's builder, GCC 8.4). Do not raise it: under `-std=gnu++14` some libraries no longer compile. So no C++14 features - `src/Utils.h` backfills `std::make_unique`, and every file that calls it must include `Utils.h` itself rather than rely on another header having pulled it in.
- `static constexpr` arrays as class members in header-only adapters cause ODR linker errors under C++11 (no inline variables). Declare them as local `constexpr` variables inside the static method instead.
- **FastLED is a pinned, load-bearing dependency (3.9.16).** See *READ FIRST*.
  The project uses only `addLeds`, `show`, `clear`, `setBrightness`,
  `setMaxRefreshRate` and `CRGB` - **none** of its colour engine (no `CHSV`,
  palettes, blends). It is acting purely as a WS2812 shift-out driver, so replacing
  it is ~5 call sites plus a `CRGB` shim. Options are evaluated in the playground
  repo's CLAUDE.md; the leading candidate is NeoPixelBus with a **DMA (I2S)**
  method, because naming the peripheral explicitly avoids the silent
  driver-swap-by-IDF-version that caused the core 3.x regression. Not yet decided -
  only forced if V2's LED output misbehaves (V2 shares the pinned platform, so it
  also runs RMT4). `FASTLED_USES_ESP32S3_I2S` is **not** a fallback on the pinned
  core: the build warns that `esp_memory_utils.h` is missing and the parallel
  clockless I2S driver is unavailable.
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
One `ModeMenuEntry` table in `ModeSwitchingView.h` drives every row: OLED label, e-paper label, LED word, target `DeviceModeState` and colour. They used to be four hand-aligned lists plus an enum, which drifted; add a row, do not add a list. `enabled` hides a row at runtime (`Board::HAS_PLAYER_SETUP`) - never an `#if` here.

Member declaration order in that view is load-bearing: `entryIds` feeds `optionsList`, which `Scrollable` binds **by reference** and whose size it snapshots at construction. Both are `const` and never resized afterwards.

The V1 bar is split into one segment per **visible** row, left to right in menu order, and lights the selected row's segment in its colour: `ModeSwitchingBarRenderer` takes the row's position among visible rows and their count, never a hand-kept slot. A slot column used to live in the table, drifted from the menu order (PADEL lit the rightmost segment) and left PROFILE and CONFIG without one.

### Pinout
All pins live in `src/Board.h`, in **one shared block** (both boards are the same module on the same wiring), checked by `pinIsSafe()` static_asserts.

| Function | GPIO (both boards) |
|---|---|
| Remote A / B / C / D (prev / next / undo / enter) | 8 / 10 / 13 / 14 (`INPUT_PULLDOWN`: unwired pins float and fire phantom presses; the receiver drives its output, so it wins) |
| WS2812B data | 18 |
| Buzzer (active high) | 3 (V2 via MOSFET, V1 direct) |
| OLED I2C SDA / SCL | 4 / 5 (26-32 are SPI flash, 33-37 octal PSRAM on N16R8) |
| E-paper SCK / MOSI / CS / DC / RST / BUSY | 12 / 11 / 9 / 15 / 16 / 17 (unconnected on V1) |
| Battery ADC (ADC1, 10k/10k divider) | 6 |

**V2 LED slots** (verified on the device 2026-09-17; several differ from the schematic-era notes):
- 0,1 border bottom-left; 2,3 top-left; **4 back indicator B**; 5,6 bottom-right; 7,8 top-right; **9 back indicator A**.
- Digit modules are chained in **reverse**: A (leftmost) 58-73, B 42-57, C 26-41, D 10-25. Per-module slot 2 is dead: 12, 28, 44, 60 are never written — it is a chain position on the right column with no die fitted, not a bottom-row LED.
- **Physical LED coordinates live in `assets/led-map.svg`** (vector, authoritative; `led-map.png` is a raster preview). `helpers/led_positions.py` flattens it into `Animation/LedSlotPositions.h`; `--check` verifies the committed table still matches. The ASCII drawing in `NineSegmentProfile.h` is schematic and **not to scale** — never derive geometry from it. Each module is 3x6 dies on a uniform 197-unit grid (~16 units/mm), and 5 slots per module drive 2 parallel dies.
- **V1's map is `assets/v1_led_map.svg`** (same conventions and pitch). `python helpers/led_positions.py [v1|v2] [--check] [--preview out.png]` generates or verifies either board's table. V1's dies carry no index markings, so the generator takes each segment's indices from `SevenSegmentProfile.h` and orients them by minimising wire length along the ascending-index chain; only the colon is unpinned (both dies equidistant from the origin), so index 0 = upper die by convention.
- On the bench, LEDs and buzzer need **battery power**; USB alone does not feed the 5 V rail.

### Input
- `RemoteInputManager` — manages 4 `RemoteInput` buttons (A/B/C/D) triggered by GPIO interrupts from the 433 MHz receiver. Use `button.takeActionIfPossible(debounceMs)` in views.
- The 433 MHz receiver generates multiple RISING edges per button press (RF noise). `RemoteInput::trigger()` must guard against this — do not remove the debounce check without understanding the double-trigger bug.
- **Measured receiver behaviour (V2 bench probe, 2026-09-18, 37 presses).** The output is a **clean latch**: one contiguous HIGH per press, LOW on release, **zero dropouts**, idle driven LOW. Taps run **236-360 ms** (median 282); deliberate holds run 2498-7595 ms and scale linearly. Same part on both boards. This is why long-press detection is a **level poll** (`RemoteInput::poll()`, every loop pass) and not edge timing — do not reintroduce a gap/burst heuristic.
- **On the three `*TournamentChoosePlayersView`s, short C = back to the mode selector** and D alone toggles a player or presses START. That is exactly what a long press already did there, which is what let the `KONIEC` row go.
- **Long press on C = back one step** (`LONG_PRESS_MS = 2000`, one shot per press). `DeviceMode::goBack()` returns false when there is nowhere to go; `main.cpp` then stays silent, and that silence *is* the feedback. `GamePlaying` returns false as an **explicit case** so a hold can never discard a live game — it is a deliberate exception, not a missing case. Every fire is logged, so a dead detector is distinguishable from an intended no-op.
  - The threshold is 2000 ms because people undershoot their own count: holds aimed at "3 seconds" measured 2498-2951 ms, so a 3000 ms threshold missed 7 of 13. Longest tap was 360 ms, so the margin is still 5.5x. **Do not raise it back to a round 3000.**
  - **Button C alone fires its short action on release**, not on press (`setDeferToRelease`), costing ~280 ms — otherwise a hold performs undo/confirm on its way to the back. A/B/D stay instant on press, so scoring keeps its latency.
- `Buzzer` (`src/Buzzer.h`, GPIO 3) plays a short tone on remote presses and a victory theme on game win (`onMatchOver(CelebrationVariant)`; `main.cpp`'s `playMatchOver()` picks `playBajgiel()` or `playCelebration()`; fired once from the `GameCelebration` case of `handleStateChange()` - never on the summary, never on an overlay restore). Applied live by the apply-on-save choke point (see Persistence).
  - **Buzzer mode** - `PrefsData.buzzerMode` is tri-state (`PrefsBuzzer`: 0 off, 1 always, 2 in match; default 2). A stored 1 stays "always" - no migration - and an OTA downgrade reads 2 as bool on. Any other byte is treated as always (`PrefsBuzzer::normalize`, which is also the CONFIG tickbox state and the `/api/settings` value).
  - "In match" = a sport mode past `TournamentChoosePlayers`: the pure virtual `DeviceMode::isInMatch()`, fed to `Buzzer::setInMatch()` every loop pass before the long-C handling and `deviceMode->loop()`. It gates the press tick, the back double-beep and the celebration/bajgiel themes, judged when the sound is requested (so a long C from MatchStartGame beeps). The low-battery warning ignores it; only off silences it.
  - CONFIG cycles NIE -> MECZ -> TAK (D forward, C back, wrapping), Red / Yellow / Green on the glyphs, indicators and V1 bar (`ConfigBarRenderer::buzzerColor`); LEDs keep `buZZ`, the OLED lists labels only.
- **Never call `ESP.restart()` directly — use `safeRestart()` (`src/SafeRestart.h`).** Every pad returns to a floating input at reset and GPIO 3 has no default pull, so on V2 the MOSFET gate floats and the buzzer sounds through the reboot. A LOW written before the restart is discarded; `safeRestart()` latches the pad with `gpio_hold_en()`, which survives a *software* reset. `Buzzer::init()` releases it (`pinMode` → `LOW` → `gpio_hold_dis`, in that order — driving before unlatching leaves no floating gap) and runs as the **first** statement of `setup()`, before `Serial`/prefs/`initHardware()`. Crash, watchdog, brownout and power-on resets are not covered; only a hardware pull-down on the gate would fix those. An OTA *downgrade* to firmware without `gpio_hold_dis` leaves the buzzer muted until a power cycle.
- Powered by 1S2P INR18650-35E battery with 2A boost converter / charger.
- **Garmin watch as a BLE remote** was researched, not built (task #66): feasible on both boards (BLE and WiFi never run together), ~0.5 s latency fixed by Connect IQ, needs explicit watch<->board pairing. Read `docs/garmin-remote-poc.md` before any BLE work; the Garmin App Remote wire protocol (advertising, GATT, auth, pairing, commands, state) is specified in `docs/garmin-protocol.md` - #80 and #81 implement it, change it there first.

### Persistence & Networking
- `PreferencesManager` — reads/writes `PrefsData` (WiFi SSID/password, brightness, AP mode) to ESP32 NVS. `wifiIpAddress` is **live state, never persisted**: empty until an interface comes up, set from `WiFi.localIP()` on an STA connect and `WiFi.softAPIP()` in both AP paths, cleared when WiFi is off or an AP drops. The CONFIG footer omits its line while it is empty, which is why it defaults to empty rather than to a placeholder.
- **`enableDevMode` defaults to 0.** A device with empty or invalid NVS boots with STA, AP, OTA and telnet all off, instead of spending ~15 s failing STA against an empty SSID and then raising an open AP. Stored blobs keep their own value, so V1's OTA path is untouched. Recovery on a wiped device is CONFIG -> Dev Mode ON -> `[RESTART]`, from the remote alone.
  - The CONFIG row is labelled **Dev Mode**, not WiFi: all it exclusively buys is joining the house network at boot, and PROFILE raises its own AP regardless, so "WIFI" read as if the roster editor needed it. The label is the same in both language branches. It is the one CONFIG row with **no LED word** - the 46-glyph table has no W, K, M or V, so neither DEV nor MODE can render; the indicators' green/red carries the state. On the e-paper the label is 2 px wider than the tickbox row allows and is clipped, deliberately - clip-never-shrink.
- **Apply-on-save choke point.** `PreferencesManager::save()` calls `apply()` after `putBytes`; `main.cpp`'s `applySettings` sets brightness and the buzzer. Every writer (CONFIG exit, the web settings, `/connect`, `/settings/dev`) is therefore live without applying by hand - the CONFIG buzzer toggle no longer needs a reboot. `setup()` calls `apply()` once, after `initHardware()` and before the boot sweep. Dev Mode is deliberately not applied (network topology, read at boot and by `disablePlayerSetupAp()`); the handler must never block, restart or touch WiFi.
- `PrefsData` lives in `src/PrefsData.h` (host-includable, `static_assert(sizeof == 131)`, plus the level <-> byte helpers `PrefsBrightness::levelToByte` / `byteToLevel`).
- `RemoteDevelopmentService` — provides OTA firmware updates and WiFi-based serial logging. It logs the boot WiFi outcome (IP or fallback AP): a naked or sealed board has no OLED to read it from.
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

### Strings (UI language)
Every user-visible string is a `constexpr const char* const` in `src/Strings.h`, chosen by `#if LANG_PL` / `#else`. **One language per build**: `-DLANG_PL` is in `build_flags` for both boards (in `[s3]`; every firmware env inherits it via `extends`), so the unselected branch never reaches the preprocessor. Never index a two-row table at runtime - that ships both languages.

- The device is Polish. The English table stays as the unbuilt `#else` branch for reference; **edit both sides** or the other language silently rots.
- **No diacritics anywhere.** Every GFX font declares range `0x20-0x7E` and `GlyphMasks.h` has no accented glyphs, so words that would need one were *replaced*, not stripped: `SIATKA` (not siatkowka), `NISKA` (not slaba).
- LED words are constrained further, to the 46-glyph table - **no W, K, M or V**. Return is `COFNIJ`, not `WSTECZ`, because the LEDs, OLED and e-paper are readable at once and must agree.
- Polish numerals decline (1 gracz / 2-4 gracze / 5+ graczy), so a `"%u <noun>"` format string is wrong for some counts. Use a label-colon form (`"W GRZE: %u"`).
- The `_OLED` variants exist because the OLED list is space-padded for centring at a fixed x (10 chars max, `FreeMono9pt7b`), while the e-paper rows are not.
- Out of scope: `printLn`/telnet/serial logs, the WiFi config web page, player names, the AP SSID/password, and the `BAT` / `FW` abbreviations.
- Verify a language change with `grep -ac "<english word>" .pio/build/v2/firmware.bin` - it must return 0.

### Players / profiles
The roster is **data, not code**: up to 32 profiles in NVS, edited from a phone (see *Roster editor*). The six `FACTORY_PLAYERS` in `main.cpp` are only the fallback, used when NVS holds nothing valid and by "restore factory profiles".

- `PlayerRoster` (`src/PlayerRoster.h`, board-agnostic) owns the profiles and exposes `profiles()` as `std::vector<UserProfile *> &`, so every mode's signature is unchanged. Built exactly once, by `load()` in `setup()`.
- Its own NVS key `"ply"` in namespace `"ns"` - **never a field in `PrefsData`**, whose `read()` rejects the blob unless `getBytesLength == sizeof(PrefsData)`, so growing it would drop brightness *and* the WiFi credentials.
- NVS usage: the roster ("ply") blob is ~608 B and `PrefsData` ("set") is 131 B, both in namespace "ns", against V1's 20,480 B NVS partition — under 5% used, so headroom is not a concern for either blob.
- Blob is fixed-size `PlayersData` v2. `readBlob()` accepts only an exact v2 blob (length, version, count) and seeds the factory list otherwise. The v1 (no uid) migration was dropped once both devices had run v2 firmware, so a v3 must bring its own v2 migration, or an update wipes the roster.
- **Two identifiers, deliberately** (`UserProfile`): `id` is the position in this boot's roster - what the LEDs show as `P  3` and what `MatchOrderKeeper` keys on - and is renumbered by any reorder. `uid` is a `uint32_t` from `esp_random()`, stored beside the name, and survives rename/recolour/reorder. Use `id` inside a match, `uid` for anything outliving one. Nothing persists an `id`, which is what makes positional ids safe.
- Colours come from `PlayerPalette` / `PlayerColors` (`src/PlayerPalette.h`), **not** `Colors::` - those are UI accents. 16 entries, picked by the user on the LEDs (2026-09-24) to replace a screen-derived set whose pale entries washed out; none trips the editor's bright or dark warning. Read the caveat block in that header before changing them: several entries share a hue (Pomarańczowy/Brązowy, the three greens, Czerwony/Magenta/Różowy) and a WS2812 conveys hue far better than lightness. Changing an entry recolours nobody - stored players keep raw RGB and show as custom until re-picked.
- The `/save` colour field is a palette **index** for a preset, or `#RRGGBB` (`PlayerPalette::fromHex`, strict) for a custom colour - the blob already stores raw RGB, so no layout change. The page shows a stored colour as a preset only on an exact match (`isPreset`); anything else, including one left over from an older palette, renders and re-saves as its own hex, never snapped to the nearest preset.
- Duplicate names and duplicate colours are both allowed and unwarned; the `uid` is what makes two people called Krystian two people.

### Roster editor
`PlayerSetupMode` + `PlayerSetupView`, reached from the mode selector ("PROFILE"). **One implementation on both boards** (ported to V1 2026-09-23); the only difference is V2's e-paper QR placard, which is a no-op through the `EInkDisplay` stub on V1. Entering **forces the AP up** whatever `enableDevMode` says (`RemoteDevelopmentService::enablePlayerSetupAp()`: tears STA down, no blocking delay); the editor never runs over the house network. The e-paper shows a pre-rendered dual-QR placard (`EInkDisplay::showImage()`, always a full refresh - a ghosted QR will not scan). C or D exits, and it closes itself after 15 minutes idle. The same web UI is also reachable over the house network while Dev Mode is on and STA is connected (see the gate below) - for debugging without the AP step.

- The AP is raised in the mode's **constructor** and dropped in its **destructor**, not in a button handler, so every exit path tears it down identically.
- **Leaving without saving rejoins the house network without a reboot** - load-bearing on V1, which is flashed only over OTA. `disablePlayerSetupAp()` drops the AP and, only when `enableDevMode` is on, starts a non-blocking `WiFi.begin()` (no delay, no AP fallback); `checkStaReconnect()` in `RemoteDevelopmentService::loop()` sees `WL_CONNECTED` and restores the IP and telnet. If STA has not connected within 10 s (boot's budget) it raises the setup AP without blocking, AP-only like boot's fallback (a retrying STA glitches the LEDs, see READ FIRST), so wrong stored credentials never leave the device with neither STA nor AP; the house network returns only after a reboot. `setupOTA()` is idempotent (`if (OTAServer) return;`), so no route is registered twice.
- AP identity is one pair, `RemoteDevelopmentService::AP_SSID` / `AP_PASSWORD`, used by both AP paths and by the OLED screen. `helpers/player_setup_qr.py` bakes the same values into the placard - change them together.
- **OLED discovery screen** (both boards, the only one V1 has): title fixed on line 0, lines 1-2 show one label/value pair per 1.5 s: WiFi/SSID, password, IP. Pairs, not a one-line step, which put a value over the next pair's label. No exit hint - C/D exits and needs no reminder. It renders per tick on its own clock, but a queued render (`restoreView()` after an Overlay) redraws at once. `FreeMono9pt7b` advances 11 px per glyph, so 11 characters fit 128 px - exactly `192.168.4.1`.
- `PlayerSetupWebUi` (`src/Web/PlayerSetupWebUi.{h,cpp}`, no `#if BOARD_REV`) lives at **file scope in `main.cpp`**, because `WebServer` (core 2.0.17) has no `removeHandler`: a route handler outlives every mode, so no route lambda may capture a view. Requests are refused by a gate, not by unregistering.
- **Pages are real files in `web/`** (`profile`, `settings`, `update` `.html`/`.js`, one `app.css`; UTF-8, openable in a browser). `helpers/web_assets.py` (a `pre:` script after `version_increment.py`, also runnable standalone) minifies each, then gzips it (`mtime=0`), round-trip checks it, fails the build on a missing file or non-UTF-8, prints raw/min/gz sizes and rewrites the gitignored `src/Web/WebAssets.generated.h` only when its bytes change. `WebAssets::serveAsset` sends them from flash with `send_P` and an explicit length, always `Content-Encoding: gzip` + `Cache-Control: no-store` - never through a String. The pages are static; data comes from JSON.
- **Keep `web/` readable; the build minifies it.** Only flash size counts (the browser gets the minified text), and gzip alone leaves indentation costing ~6 %. The minifier is built in, no pip or Node: JS on jsmin's rules (its output is byte-identical to `rjsmin`, CSS to `rcssmin` - the drop-in fallback if it ever misbehaves), HTML drops comments and whitespace between tags that spans a line break (the pages are flex-gap layouts), keeping quoted attribute values verbatim. The supported subset fails the build loudly otherwise: **no JS template literals**, no `<pre>`/`<textarea>` or inline `<script>`/`<style>`. After touching the minifier run `python helpers/web_assets.py --selftest` (fixed input -> output cases, writes nothing) and add a case for what you changed.
- **One route table** in `PlayerSetupWebUi::registerRoutes()`. `gated()` is the only place the gate and its 503 live, and the rule itself is `WebAccessGate::open()` = PROFILE open **or** (Dev Mode on **and** STA connected). Gated: `GET /api/roster`, `GET /api/settings`, `POST /save`, `POST /preview`, `POST /settings`, `POST /settings/preview`. Ungated: the pages and assets, `GET /api/device` (`{fw, ssid, hw}`; `hw` = `Board::NAME`, shown on `/update`) and `POST /settings/dev`. `POST /connect` (legacy) and `POST /update` stay in `RemoteDevelopmentService`, registered unconditionally. **`GET /update`, `GET /settings`, their assets, `/api/device`, `POST /settings/dev` and `POST /update` carry no gate and no `#if` - they are how a sealed V1 gets new firmware and new Wi-Fi credentials, so keep it that way.** Outside PROFILE the colour preview has no LED effect (only `PlayerSetupView` draws it) and a brightness preview reverts by itself after 60 s.
- **Ustawienia (`/settings`)** has two panels, each with its own save. "Wyświetlacz i dźwięk": brightness as one `.sl` slider 1-8 (warning at >= 7) and the buzzer as three `.seg` buttons (Zawsze `1` / W meczu `2` / Wyłączony `0`; the validator takes exactly `0`-`2`), saved by `POST /settings` (brightness, buzzer and the *stored* Dev Mode; `SettingsValidator::stage`), applied live, no restart; `POST /settings/preview` (`level=N` / `cancel=1`) sets the LEDs through a callback into `LedDisplay::setBrightness`, never `save()`; `close()` and the page's `pagehide` beacon revert it. "Dla programistów" (red outline + warning): the Dev Mode toggle (shown only when the gate is open) and **the Wi-Fi form, always visible whatever `/api/settings` answers** - the recovery path on a sealed V1's own AP, and a plain form, so it works with JS off. It posts to `POST /settings/dev` (`SettingsValidator::stageDev`): ssid 1-63 and password 0-63 bytes required; an empty password keeps the stored one for the same SSID and means an open network for a new one; `devMode` is accepted only while the gate is open (403 otherwise); all-or-nothing, then one `save()` and the deferred `armRestart()` (never `delay()`).
- **Validation is pure and host-tested**: `RosterSaveValidator` (a `FormLookup` + injected uid generator, same Polish messages), `SettingsValidator`, `RosterJson::build` (the `/api/roster` body; a 10-digit uid once overflowed a `char[12]` and blanked the page) and `WebJson::appendString` (the one escaper). Suites `test_web_roster` and `test_web_settings`; run them after touching `src/Web/`. `PreviewState` owns the colour-preview slots; `PlayerRosterData.h` / `PrefsData.h` hold the blob structs with `static_assert`s on their sizes.
- `POST /preview` (form-encoded `playerId` + `color`, gated like `/save`) is the colour dialog's live LED preview. It only stores state in `PlayerSetupWebUi` - two slots; the edited player is always on the left and the previous left one slides right (a player already on the right swaps sides) - never NVS, never a draw; `PlayerSetupView` reads it (`takePreviewDirty`/`hasPreview`/`previewSlot`) and renders `P<l>P<r>` in both colours with the indicators, border and colon dark. Reset in `open()`/`close()`. Last POST wins from any client; the page keeps one request in flight (so does the brightness preview). Opening the dialog previews that row's current colour at once; `Wybierz` commits to the row, `Anuluj` re-previews the original colour; The sheet toggles between two tabs, `Paleta kolorów` (the preset grid) and `Własny kolor`, one view at a time so it fits a phone; it opens on the tab matching the stored colour. `Własny kolor` shows in-page hue/saturation/brightness sliders, never `<input type=color>` (Android's native one is a fixed swatch grid with no free choice) and never `<input type=range>` either: a finger drifting off a native range drops the drag and the handle snaps back. Each slider is a 56 px `.sl` zone that `setPointerCapture`s the pointer, so only the horizontal position counts until the finger lifts. The page is phone-only. The top of the dialog warns on r+g+b >= 574 (75 % of full white - near-white is the highest draw; `#00FFFF` at 510 does not trip it) and on r+g+b < 77 (under 10 %, barely visible on a WS2812).
- The save is server-authoritative and atomic: every field validated on its own, whole blob staged in RAM, one `putBytes`, then a deferred `safeRestart()`. Any rejection is a 400 and leaves NVS untouched.
- The page must post `application/x-www-form-urlencoded`. Verified in the core's `Parsing.cpp`: `WEBSERVER_MAX_POST_ARGS` (32) caps only `_parseForm` (multipart); urlencoded bodies go to `_parseArguments`, which allocates per `&`.
- The page is **Polish-only and not in `Strings.h`** - it is rendered by a browser, so it carries real diacritics, unlike every GFX-font string. Profile *names* stay printable ASCII because those do reach the LED/OLED/e-paper fonts.
- The placard has **no title bar** - the two QR codes and their captions need the full 296 px, and the menu entry has just named the screen. It is a baked bitmap and cannot read `Strings.h`; its wording lives in `helpers/player_setup_qr.py`. Regenerate with `python helpers/player_setup_qr.py` (needs `pillow` and `qrcode`) after changing the AP name, password, URL or captions.

### Battery (both boards)
`BatterySensor` samples GPIO 6 at most every 200 ms into a rolling average (never block in `loop()`), with explicit 11 dB attenuation. `Board::BATTERY_FACTOR` is per board: V2's 2.027 was calibrated against a meter on core 2.0.17; V1 starts at the same value, **uncalibrated** until measured on the soldered unit. Volts appear only in the log (every 10 s, logged even when implausible); screens show percent.
- `available()` means a **plausible 1S pack**, 2.5-4.5 V: an unsoldered divider floats (a naked board read 5.5 V, then 0.1 V), and either would latch the low state and cap brightness. Out of range, `BatteryMonitor` stops updating and the log line says `(implausible, ignored)`.

`BatteryMonitor` (`src/BatteryMonitor.h`) turns that voltage into what the user sees. Board-agnostic — **no `#if BOARD_REV`**; the low overlay, cooldown and brightness cap work identically on both boards.
- `voltsToPercent()` interpolates one curve: the **midpoint** of the resting-OCV and 10 W-load columns for the 1S2P INR18650-35E pack (3.000 V = 0 %, 4.175 V = 100 %, 10 % steps). It is a local `constexpr` inside the static method (a `static constexpr` array member is an ODR link error on GCC 8.4) and the single place to retune the mapping.
- Two separate numbers, deliberately: the **mapped** percent drives the thresholds, the **shown** percent (5 % steps, only ever falling, jumping up only on a >= 10 point rise) is what displays print — otherwise the readout flickers as LED load sags the cell.
- Low state: mapped <= 10 % held for 60 s continuously; clears above 15 % (hysteresis), so a single LED-load sag does not trip it. `takeLowWarning()` is the one-shot that fires the overlay.
- The overlay is additionally rate-limited to one per `WARNING_COOLDOWN_MS` (5 min). Hysteresis alone is not enough: under LED load the voltage still floats across the 10/15 pair, so `low` clears and re-latches and the one-shot re-arms every time. The cooldown gates only the *warning* - `isLow()`, and therefore the brightness cap, keeps tracking the live state.
- While low, `main.cpp` sets `LedDisplay::setBrightnessCap(31)` (menu level 1). The cap is **not** persisted and callers never see it: `setBrightness()` stores what was requested and applies `min(requested, cap)`, so `ConfigView`'s brightness edits stay capped on their own. Always set brightness through `LedDisplay`, never `FastLED.setBrightness` directly.
- Shown in the mode selector and CONFIG: on the e-paper footers (V2) or top right of the OLED (V1, see `BackDisplay`).

### `lib/` directory
`lib/` holds only PlatformIO's stock `README` and no code. All project source is under `src/`.
