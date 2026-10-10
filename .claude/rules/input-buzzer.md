---
paths:
  - "src/RemoteInput/**"
  - "src/Buzzer.h"
  - "src/SafeRestart.h"
  - "test/test_buzzer_mode/**"
---

# Remote input and buzzer

- `RemoteInputManager` — manages 4 `RemoteInput` buttons (A/B/C/D) triggered by GPIO interrupts from the 433 MHz receiver. Use `button.takeActionIfPossible(debounceMs)` in views.
- The 433 MHz receiver generates multiple RISING edges per button press (RF noise). `RemoteInput::trigger()` must guard against this — do not remove the debounce check without understanding the double-trigger bug.
- **Measured receiver behaviour (V2 bench probe, 2026-09-18, 37 presses).** The output is a **clean latch**: one contiguous HIGH per press, LOW on release, **zero dropouts**, idle driven LOW. Taps run **236-360 ms** (median 282); deliberate holds run 2498-7595 ms and scale linearly. Same part on both boards. This is why long-press detection is a **level poll** (`RemoteInput::poll()`, every loop pass) and not edge timing — do not reintroduce a gap/burst heuristic.
- **Long press on C = back one step** (`LONG_PRESS_MS = 2000`, one shot per press). `DeviceMode::goBack()` returns false when there is nowhere to go; `main.cpp` then stays silent, and that silence *is* the feedback. `GamePlaying` returns false as an **explicit case** so a hold can never discard a live game — it is a deliberate exception, not a missing case. Every fire is logged, so a dead detector is distinguishable from an intended no-op.
  - The threshold is 2000 ms because people undershoot their own count: holds aimed at "3 seconds" measured 2498-2951 ms, so a 3000 ms threshold missed 7 of 13. Longest tap was 360 ms, so the margin is still 5.5x. **Do not raise it back to a round 3000.**
  - **Button C alone fires its short action on release**, not on press (`setDeferToRelease`), costing ~280 ms — otherwise a hold performs undo/confirm on its way to the back. A/B/D stay instant on press, so scoring keeps its latency.
- `Buzzer` (`src/Buzzer.h`, GPIO 3) plays a short tone on remote presses and a victory theme on game win (`onMatchOver(CelebrationVariant)`; `main.cpp`'s `playMatchOver()` picks `playBajgiel()` or `playCelebration()`; fired once from the `GameCelebration` case of `handleStateChange()` - never on the summary, never on an overlay restore). Applied live by the apply-on-save choke point (see Persistence).
  - **Buzzer mode** - `PrefsData.buzzerMode` is tri-state (`PrefsBuzzer`: 0 off, 1 always, 2 in match; default 2). A stored 1 stays "always" - no migration - and an OTA downgrade reads 2 as bool on. Any other byte is treated as always (`PrefsBuzzer::normalize`, which is also the CONFIG tickbox state and the `/api/settings` value).
  - "In match" = a sport mode past `TournamentChoosePlayers`: the pure virtual `DeviceMode::isInMatch()`, fed to `Buzzer::setInMatch()` every loop pass before the long-C handling and `deviceMode->loop()`. It gates the press tick, the back double-beep and the celebration/bajgiel themes, judged when the sound is requested (so a long C from MatchStartGame beeps). The low-battery warning ignores it; only off silences it.
  - CONFIG cycles NIE -> MECZ -> TAK (D forward, C back, wrapping), Red / Yellow / Green on the glyphs, indicators and V1 bar (`ConfigBarRenderer::buzzerColor`); LEDs keep `buZZ`, the OLED lists labels only.
- **Never call `ESP.restart()` directly — use `safeRestart()` (`src/SafeRestart.h`).** Every pad returns to a floating input at reset and GPIO 3 has no default pull, so on V2 the MOSFET gate floats and the buzzer sounds through the reboot. A LOW written before the restart is discarded; `safeRestart()` latches the pad with `gpio_hold_en()`, which survives a *software* reset. `Buzzer::init()` releases it (`pinMode` → `LOW` → `gpio_hold_dis`, in that order — driving before unlatching leaves no floating gap) and runs as the **first** statement of `setup()`, before `Serial`/prefs/`initHardware()`. Crash, watchdog, brownout and power-on resets are not covered; only a hardware pull-down on the gate would fix those. An OTA *downgrade* to firmware without `gpio_hold_dis` leaves the buzzer muted until a power cycle.
