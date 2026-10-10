---
paths:
  - "src/main.cpp"
  - "src/DeviceMode/**"
---

# Main loop, modes and views

Every view also answers the Garmin watch (`describeForWatch`, `handleWatchCommand`, a stub in `test/test_garmin_views/view_stubs.h`): read `garmin.md` before adding a view or changing its input.

### Top-Level Loop (`src/main.cpp`)
Hardware is initialized in `setup()`. On both boards `ledDisplay.playBootSweep()` runs straight after `einkDisplay.begin()` (a stub on V1) — a blocking ~1 s LED boot sweep, deliberately placed **after** the e-paper's ~3 s init so it plays against the splash rather than a blank panel. `setup()` calls `einkDisplay.flushRefresh()` in between: the splash is only *requested* by `begin()` and would stay queued while the sweep blocks, since `loop()` has not started to pump `update()` yet. The main `loop()` runs at ~20fps (50ms tick). It polls `RemoteInputManager` for input and delegates to the active `DeviceMode`. `einkDisplay.update()` and `batterySensor.loop()` run on **every** pass, before the 50 ms gate (the e-paper polls its BUSY pin). No custom FreeRTOS tasks.

An `Overlay` (`src/Display/Overlay.h`) is the one thing that outranks the active mode: while `overlay.active()` it renders to all three displays and `deviceMode->loop()` is **skipped**, so the mode is *paused*, not changed — no input, no rendering, state and timers intact, and a pending score commit lands on the first frame after (the commit check lives in `handleInput`). On the pass it ends, `main.cpp` calls `RemoteInputManager::clearLatches()` (presses made during the overlay must not act on the view coming back), `Overlay::resetLedState()` and `DeviceMode::restoreView()`. The overlay knows nothing about modes, views or sports — it is content only (title, line, 4 glyphs, colour, duration), so #17's shutdown warning reuses it.

## DeviceMode + View detail

- `renderEInkDisplay(EInkDisplay&)` is the third, non-pure hook (default: blank). It is called every frame, but the e-paper must refresh only on real change: views pass **values** to `EInkDisplay` (`showMatchScore`, `showBlank`), which compares them with what is shown. Never rely on `queueRender()` for it (GamePlaying views never call it). Start each override with `if (!einkDisplay.available()) return;` so V1 computes nothing for its stub.
- View flow per sport mode: `TournamentChoosePlayers` → `MatchStartGame` → `MatchIntro` → `GamePlaying` → `GameCelebration` → `GameOver` → back to `MatchStartGame`. Padel reaches `MatchIntro` once per set (never per gem), purely through its flow.
  - `MatchIntro` and `GameCelebration` are one template view each, shared by the three sports (`DeviceMode/Intro/MatchIntroView.h`, `DeviceMode/Celebration/GameCelebrationView.h`; copies of one shape, no shared base). Each ends when its animation reports inactive, on a skip (intro: any button; celebration: C/D, with `preventTriggerForMs()`), or on a second `initLedDisplay` (`restoreView()` after an Overlay - no replay). Long C goes back to `MatchStartGame` from both. A view that ends on its own timer must hand over **without** drawing its last frame: an expired layer leaves only the base, a visible black frame before the next view.
  - Their OLED and e-paper show exactly what the neighbouring view shows (intro: GamePlaying's first frame; celebration: the summary), so the e-paper refreshes on entry and not at the hand-over.
  - `GameCelebration` hosts every end-of-game variant: `CelebrationVariant.h`'s `selectCelebrationVariant()` returns `Bajgiel` when either score in the `GameResult` is 0 (squash 11-0, volleyball to 0, a padel set 6-0; a tiebreak set never is), else `Normal` (the sweep). Sport-agnostic, `Rules` untouched.
  - **Bajgiel**: `LedDisplay::startBajgiel(loserOnLeft)` puts `Animation/LedBajgielAnimation.h` on layer 2 as a grey comet (`Multiply` + `LitOnly` on the loser's score, no colour of its own - the base glyph is already the loser's colour): both 0s spin clockwise in phase for 5 s (1 s per turn, half-turn tail). Each slot's angle is computed once in `start()` around its digit's **bounding-box** centre (V2's dead slots would bias a mean); the render is integer-only. When it expires the base is the summary's frame. `celebrationActive()` / `stopCelebration()` cover both variants. The OLED shows `BAJGIELOLED_BITMAP`, the e-paper `BAJGIEL_EINK_BITMAP` (`EInk/Images/Bajgiel.h`, `nullptr` on V1 like `PlayerSetupQr.h`), then the summary repaints both.
  - `GameCelebrationView` checks `celebrationActive()` **before** `display()`, so a view ending on its own timer never draws a last frame.
  - Each sport mode's `handleStateChange()` starts with `remoteInputManager.clearLatches()`: the timer-driven hand-overs call no `preventTriggerForMs()`, and a press latched in between would otherwise act on the new view's first frame (skipping the summary, or scoring a point on GamePlaying).
- Mode transitions happen via a callback `onDeviceModeChange` passed down from `main.cpp`. It only **requests** the change (`requestDeviceMode`): the call arrives from `handleInput()`, inside the outgoing mode's own `loop()`, so constructing there would free `this` and the active view under the three render calls that still follow. `loop()` applies the swap at one point - after the `overlay.active()` gate (an overlay owns all three displays, so the incoming constructor must not draw underneath it) and before `overlay.takeFinished()`, so `restoreView()` lands on the new mode. It clears the flag, then `deviceMode.reset()`, then `buildDeviceMode()` - teardown before construction, which is what gets `PlayerSetupMode`'s AP down first. `setup()` calls `buildDeviceMode` directly.
- Views that need per-tick updates (blinking) must NOT guard `renderLedDisplay` with `if (!shouldRenderLedDisplay)` — that flag prevents every-tick rendering. Only use the guard for purely event-driven views.
- **A view must not run after the input that replaced it.** A winning commit's `Match::finishGame()` frees the `Game` the GamePlaying view points at (the view nulls `game` right after); each sport mode's `loop()` returns after `handleInput()` when `state != previousState`, so the outgoing view never renders again. It used to render once more on freed memory (found by #80, fob path too).

## Choose-players screens

- **On the three `*TournamentChoosePlayersView`s, short C = back to the mode selector** and D alone toggles a player or presses START. That is exactly what a long press already did there, which is what let the `KONIEC` row go.

### Mode selector
One `ModeMenuEntry` table in `ModeSwitchingView.h` drives every row: OLED label, e-paper label, LED word, target `DeviceModeState` and colour. They used to be four hand-aligned lists plus an enum, which drifted; add a row, do not add a list. `enabled` hides a row at runtime (`Board::HAS_PLAYER_SETUP`) - never an `#if` here.

Member declaration order in that view is load-bearing: `entryIds` feeds `optionsList`, which `Scrollable` binds **by reference** and whose size it snapshots at construction. Both are `const` and never resized afterwards.

The V1 bar is split into one segment per **visible** row, left to right in menu order, and lights the selected row's segment in its colour: `ModeSwitchingBarRenderer` takes the row's position among visible rows and their count, never a hand-kept slot. A slot column used to live in the table, drifted from the menu order (PADEL lit the rightmost segment) and left PROFILE and CONFIG without one.
