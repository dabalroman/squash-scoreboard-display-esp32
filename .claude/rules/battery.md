---
paths:
  - "src/Battery*.h"
---

# Battery (both boards)

- Powered by 1S2P INR18650-35E battery with 2A boost converter / charger.

`BatterySensor` samples GPIO 6 at most every 200 ms into a rolling average (never block in `loop()`), with explicit 11 dB attenuation. `Board::BATTERY_FACTOR` is per board: V2's 2.027 was calibrated against a meter on core 2.0.17; V1 starts at the same value, **uncalibrated** until measured on the soldered unit. Volts appear only in the log (every 10 s, logged even when implausible); screens show percent.
- `available()` means a **plausible 1S pack**, 2.5-4.5 V: an unsoldered divider floats (a naked board read 5.5 V, then 0.1 V), and either would latch the low state and cap brightness. Out of range, `BatteryMonitor` stops updating and the log line says `(implausible, ignored)`.

`BatteryMonitor` (`src/BatteryMonitor.h`) turns that voltage into what the user sees. Board-agnostic — **no `#if BOARD_REV`**; the low overlay, cooldown and brightness cap work identically on both boards.
- `voltsToPercent()` interpolates one curve: the **midpoint** of the resting-OCV and 10 W-load columns for the 1S2P INR18650-35E pack (3.000 V = 0 %, 4.175 V = 100 %, 10 % steps). It is a local `constexpr` inside the static method (a `static constexpr` array member is an ODR link error on GCC 8.4) and the single place to retune the mapping.
- Two separate numbers, deliberately: the **mapped** percent drives the thresholds, the **shown** percent (5 % steps, only ever falling, jumping up only on a >= 10 point rise) is what displays print — otherwise the readout flickers as LED load sags the cell.
- Low state: mapped <= 10 % held for 60 s continuously; clears above 15 % (hysteresis), so a single LED-load sag does not trip it. `takeLowWarning()` is the one-shot that fires the overlay.
- The overlay is additionally rate-limited to one per `WARNING_COOLDOWN_MS` (5 min). Hysteresis alone is not enough: under LED load the voltage still floats across the 10/15 pair, so `low` clears and re-latches and the one-shot re-arms every time. The cooldown gates only the *warning* - `isLow()`, and therefore the brightness cap, keeps tracking the live state.
- While low, `main.cpp` sets `LedDisplay::setBrightnessCap(31)` (menu level 1). The cap is **not** persisted and callers never see it: `setBrightness()` stores what was requested and applies `min(requested, cap)`, so `ConfigView`'s brightness edits stay capped on their own. Always set brightness through `LedDisplay`, never `FastLED.setBrightness` directly.
- Shown on the mode selector only: its e-paper footer (V2) or top right of the OLED (V1, see `BackDisplay`). CONFIG's footer keeps only the version and IP.
