#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>
#include <driver/gpio.h>

class Buzzer {
    uint8_t gpio;
    ulong offAtMs = 0;
    bool enabled = true;

    // Patterns are on/off durations in ms, terminated by 0. Function-local statics
    // rather than static class members: those are an ODR link error on GCC 8.4.
    const uint16_t *pattern = nullptr;

    uint8_t patternIndex = 0;
    bool patternPlaying = false;
    ulong patternNextAtMs = 0;

    static const uint16_t *celebrationPattern() {
        static const uint16_t steps[] = {
            60, 80,
            120, 160,
            60, 80,
            120, 0
        };
        return steps;
    }

    // Three long beeps, distinct from any in-game sound.
    static const uint16_t *lowBatteryPattern() {
        static const uint16_t steps[] = {
            300, 200,
            300, 200,
            300, 0
        };
        return steps;
    }

    // Two medium beeps: distinct from the 40 ms press tick, the celebration's
    // short-long alternation and the low-battery triple.
    static const uint16_t *backPattern() {
        static constexpr uint16_t steps[] = {
            150, 80,
            150, 0
        };
        return steps;
    }

    void playPattern(const uint16_t *steps) {
        if (!enabled) {
            return;
        }

        pattern = steps;
        patternIndex = 0;
        patternPlaying = true;
        patternNextAtMs = millis();
    }

public:
    explicit Buzzer(const uint8_t gpio) : gpio(gpio) {}

    void init() const {
        // Drive the pad before releasing the hold safeRestart() applied, so it never floats.
        pinMode(gpio, OUTPUT);
        digitalWrite(gpio, LOW);
        gpio_hold_dis(static_cast<gpio_num_t>(gpio));
    }

    void setEnabled(const bool value) {
        enabled = value;
    }

    void trigger(const ulong durationMs = 40) {
        if (!enabled || patternPlaying) {
            return;
        }

        digitalWrite(gpio, HIGH);
        offAtMs = millis() + durationMs;
    }

    void playCelebration() {
        playPattern(celebrationPattern());
    }

    void playLowBattery() {
        playPattern(lowBatteryPattern());
    }

    void playBack() {
        playPattern(backPattern());
    }

    void loop() {
        const ulong now = millis();

        if (patternPlaying && static_cast<long>(now - patternNextAtMs) >= 0) {
            if (pattern == nullptr || pattern[patternIndex] == 0) {
                patternPlaying = false;
                digitalWrite(gpio, LOW);
                return;
            }

            const bool isOn = patternIndex % 2 == 0;
            digitalWrite(gpio, isOn ? HIGH : LOW);
            patternNextAtMs = now + pattern[patternIndex];
            patternIndex++;
            return;
        }

        if (!patternPlaying && offAtMs > 0 && static_cast<long>(now - offAtMs) >= 0) {
            digitalWrite(gpio, LOW);
            offAtMs = 0;
        }
    }
};

#endif //BUZZER_H
