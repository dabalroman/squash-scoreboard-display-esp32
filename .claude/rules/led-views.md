---
paths:
  - "src/DeviceMode/**"
  - "src/Display/LedDisplay/**"
---

# LED display: what views must do

The engine itself is in `led-engine.md`.

- **V2 has no history bar.** `setLedBarState` takes a **lambda** (`[&] { return XBarRenderer::toLedBarPixels(...); }`), never pixels: an argument is evaluated even into an empty setter. `resetAnimations` (the old `resetHistoryBar`) stops every layer animation (celebration, intro, breathing) on both boards and also clears the bar on V1. Keep `LedBar::PIXEL_COUNT = 24` on both boards (`MatchResultBarRenderer` breaks at 0).
- The celebration is **layered, not a takeover**: layer 2, target `Front`, `AllSlots`, so score, colon, border and the V1 bar stay readable under it; the indicators are outside `Front`. It runs in `GameCelebrationView` (2.4 s, then the summary); the `GameOver` summary is static and calls `stopCelebration()` (layer 2 only) + `setBreathing(0)`, **not** `resetAnimations()`, which would also clear V1's history bar the summary still shows. An `Overlay` frame calls `resetAnimations()`.
- Layer 1 = game-ball breathing (GamePlaying). Layer 2 = the celebration or bajgiel (GameCelebration), the match intro (MatchIntro), or during GamePlaying the on-fire smoke or the comeback burst - never two at once.
- **Game ball outranks on fire**: views compute breathing first and pass `isOnFire && !breathe` to `onFireTargets`, so a side at game ball breathes unmasked and carries no smoke.
- **Comeback burst**: `LedDisplay::startComeback(color, onLeft)` reuses the `sweep` member with `comebackParams()` (one 800 ms cycle, band 690 - the shared band rule), origin on the scorer's half, layer 2 `Normal`/`Front`/`AllSlots`. It stops the smoke; `setOnFire()` yields while `comebackOwnsLayer2()` and retakes the layer when the burst ends. Views take the comeback side and start it **after** `setOnFire()`/`setBreathing()`, so a burst started this frame is not overwritten. `startCelebration`/`startIntro`/`startBajgiel`/`stopLayers` all stop the smoke.
- Animation instances are `LedDisplay` members (no heap). `LedBreathingAnimation` (grey level, `Multiply`, one `cosf` per frame, 1400 ms period, level 102-255) is the **game-ball breathing**: `LedDisplay::setBreathing(uint16_t targets)` is an idempotent per-frame choke point - 0 stops it, the phase restarts only on off -> on, a repeat call only moves the mask. `breathingTargets(left, right)` maps a side to `LeftScore|BorderTop|BarLeft` / `RightScore|BorderBottom|BarRight`; the three GamePlaying views call it every frame before `display()`.
- **Border (V2)** — `LedCentralScreenBorder` is its own concept, independent of the back indicators: `LedDisplay::setBorderEnabled(bool)` and `setBorderAppearance(top, bottom, blinkTop, blinkBottom)`. The indicator methods never touch it. Colours are used **as passed**, never `sameSideMode`-redirected (border faces front; indicators face back). It is the legend for the e-paper rows.
  - Every LED view calls `setBorderEnabled` in `initLedDisplay` (no implicit default, it would leak across views): off in the menus (`ConfigView`, `ModeSwitchingView`, the three `*TournamentChoosePlayersView`s), on everywhere else with the same colours as the indicators.
- Blink: `tickMs % 500 < 250` is the dark phase, shared by glyphs and border.
- **`digitToGlyph` returns `Empty` above 9**, so any slot fed a raw id blanks from profile 10 up. Both screens that show an id feed it `% 10`:
  - Padel and volleyball list **teams** and show `d` where squash shows `P` (`d5 *`, `d1d2`): a local `TEAM_PREFIX` in their picker and MatchStartGame views.
  - The three `*TournamentChoosePlayersView.h` render `P`, the ones digit, a blank, then the in/out dot - `P5 *` in (UpperDot, green), `P5 .` out (LowerDot, red). The dot's height carries the state on its own, not only the green/red tint that `setGlyphsColor(playerColor, playerStateColor)` gives slots C and D.
  - The three `*MatchStartGameView.h` render `P<id>P<id>`, two slots per player, so ids 5 and 15 read alike there; the per-player colours are what separate them.
