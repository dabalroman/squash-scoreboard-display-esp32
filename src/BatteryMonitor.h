#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <Arduino.h>
#include <math.h>

#include "BatterySensor.h"

/**
 * Turns the raw pack voltage from BatterySensor into something a user can read:
 * a stable percentage and a low-battery state with hysteresis.
 *
 * Board-agnostic - no `#if BOARD_REV` here. On V1 the sensor reports
 * `available() == false`, `loop()` does nothing and the state never changes, so
 * nothing downstream ever fires.
 *
 * Two separate numbers:
 * - the *mapped* percent (raw curve output) drives the low-battery thresholds;
 * - the *shown* percent (5 % steps, only ever falling) is what displays print,
 *   so a score screen does not flicker between 71 % and 72 % under LED load.
 */
class BatteryMonitor {
    enum : uint32_t {
        // Sample and update percentage once every 30 seconds.
        UPDATE_INTERVAL_MS = 30000,
        // The mapped percent must stay below the entry threshold this long before
        // the low state latches - a single LED-load sag must not trip it.
        LOW_HOLD_MS = 60000,
        // The voltage floats across the enter/exit thresholds as the LED load
        // varies, so the low state can latch again minutes later. The warning is
        // for the user, not for every latch: once shown, stay quiet this long.
        WARNING_COOLDOWN_MS = 300000,
    };

    enum : uint8_t {
        LOW_ENTER_PERCENT = 20,
        LOW_EXIT_PERCENT = 25,   // hysteresis: leaving low needs a real recovery
        // The shown percent jumps back up only on a change this big (charging, pack swap).
        JUMP_UP_PERCENT = 10,
    };

    const BatterySensor &sensor;

    bool initialised = false;
    uint32_t lastUpdateMs = 0;

    uint8_t mappedPercent = 100;
    uint8_t shownPercent = 100;

    bool low = false;
    bool lowTimerRunning = false;
    uint32_t lowSinceMs = 0;
    bool pendingWarning = false;
    bool warningShown = false;
    uint32_t lastWarningMs = 0;

public:
    explicit BatteryMonitor(const BatterySensor &sensor) : sensor(sensor) {}

    /**
     * Pack voltage -> percent, linearly interpolated over the calibration curve.
     * The curve is the midpoint of the resting-OCV and 10 W-load columns for the
     * 1S2P INR18650-35E pack, so it neither reads high at rest nor collapses
     * under LED load. This table is the single place to retune the mapping.
     */
    static uint8_t voltsToPercent(const float volts) {
        // Local constexpr, not a static class member: a static constexpr array in a
        // header-only class is an ODR link error on GCC 8.4 (see CLAUDE.md).
        constexpr uint8_t POINTS = 11;   // 0 %, 10 % ... 100 %
        constexpr float curve[POINTS] = {
            3.340f, 3.425f, 3.520f, 3.590f, 3.650f, 3.710f,
            3.770f, 3.840f, 3.920f, 4.030f, 4.175f
        };

        if (volts <= curve[0]) {
            return 0;
        }

        for (uint8_t i = 0; i + 1 < POINTS; i++) {
            if (volts < curve[i + 1]) {
                const float span = curve[i + 1] - curve[i];
                const float within = (volts - curve[i]) / span * 10.0f;
                return static_cast<uint8_t>(lroundf(i * 10.0f + within));
            }
        }

        return 100;
    }

    // Non-blocking, safe to call on every loop() pass. V1: returns immediately.
    void loop(const uint32_t nowMs) {
        if (!sensor.available()) {
            return;
        }

        if (initialised && nowMs - lastUpdateMs < UPDATE_INTERVAL_MS) {
            return;
        }
        lastUpdateMs = nowMs;

        mappedPercent = voltsToPercent(sensor.volts());

        const uint8_t rounded = static_cast<uint8_t>((mappedPercent + 2) / 5 * 5);
        if (!initialised) {
            shownPercent = rounded;
            initialised = true;
        } else if (rounded < shownPercent
                   || static_cast<int16_t>(rounded) >= static_cast<int16_t>(shownPercent) + JUMP_UP_PERCENT) {
            shownPercent = rounded;
        }

        updateLowState(nowMs);
    }

    bool available() const { return sensor.available(); }

    // The displayed value: 5 % steps, sticky downwards.
    uint8_t percent() const { return shownPercent; }

    bool isLow() const { return low; }

    // One-shot: true at most once per WARNING_COOLDOWN_MS window.
    bool takeLowWarning() {
        if (!pendingWarning) {
            return false;
        }
        pendingWarning = false;
        return true;
    }

private:
    void updateLowState(const uint32_t nowMs) {
        if (mappedPercent < LOW_ENTER_PERCENT) {
            if (!lowTimerRunning) {
                lowTimerRunning = true;
                lowSinceMs = nowMs;
            }

            if (!low && nowMs - lowSinceMs >= LOW_HOLD_MS) {
                low = true;

                if (!warningShown || nowMs - lastWarningMs >= WARNING_COOLDOWN_MS) {
                    pendingWarning = true;
                    warningShown = true;
                    lastWarningMs = nowMs;
                }
            }
            return;
        }

        lowTimerRunning = false;

        if (low && mappedPercent > LOW_EXIT_PERCENT) {
            low = false;
        }
    }
};

#endif //BATTERY_MONITOR_H
