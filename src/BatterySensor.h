#ifndef BATTERY_SENSOR_H
#define BATTERY_SENSOR_H

#include <Arduino.h>

#include "Board.h"

/**
 * Battery voltage from the BAT+ divider. Call sites never test the board:
 * `if (batterySensor.available())` reads identically in both builds, and on V1
 * everything inlines to constants.
 */

#if BOARD_REV == 2

namespace BatterySensorConfig {
    // Combined divider (measured 1.988) x ADC calibration, measured on core 2.0.17
    // against a meter (V2 Guidelines, Part 2, Battery sense). Never hard-code x2.
    constexpr float FACTOR = 2.027f;

    constexpr uint32_t SAMPLE_INTERVAL_MS = 200;
}

class BatterySensor {
    // Rolling average over SAMPLE_COUNT samples (~3.2 s at 200 ms).
    enum { SAMPLE_COUNT = 16 };

    uint32_t samples[SAMPLE_COUNT] = {};
    uint32_t sum = 0;
    uint8_t index = 0;
    uint32_t lastSampleMs = 0;

public:
    void begin() {
        // Explicit attenuation, never the default: ~1.9 V sits high in the 11 dB
        // range. Core 3.x rejects the call on an unread pin, so read once first.
        analogRead(Board::BATTERY_ADC);
        analogSetPinAttenuation(Board::BATTERY_ADC, ADC_11db);

        // Seed the whole window with one sample so the first readings are sane.
        const uint32_t mv = analogReadMilliVolts(Board::BATTERY_ADC);
        for (uint8_t i = 0; i < SAMPLE_COUNT; i++) {
            samples[i] = mv;
        }
        sum = mv * SAMPLE_COUNT;
        lastSampleMs = millis();
    }

    // Non-blocking: at most one ADC sample per SAMPLE_INTERVAL_MS.
    void loop() {
        const uint32_t now = millis();
        if (now - lastSampleMs < BatterySensorConfig::SAMPLE_INTERVAL_MS) {
            return;
        }
        lastSampleMs = now;

        const uint32_t mv = analogReadMilliVolts(Board::BATTERY_ADC);
        sum = sum - samples[index] + mv;
        samples[index] = mv;
        index = (index + 1) % SAMPLE_COUNT;
    }

    static bool available() { return true; }

    // Averaged millivolts at the ADC pin (before the divider factor), for calibration.
    uint32_t rawMilliVolts() const { return sum / SAMPLE_COUNT; }

    float volts() const {
        return static_cast<float>(sum) / SAMPLE_COUNT * BatterySensorConfig::FACTOR / 1000.0f;
    }
};

#else

class BatterySensor {
public:
    void begin() {}
    void loop() {}
    bool available() const { return false; }
    uint32_t rawMilliVolts() const { return 0; }
    float volts() const { return 0.0f; }
};

#endif

#endif //BATTERY_SENSOR_H
