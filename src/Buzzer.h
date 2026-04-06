#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

class Buzzer {
    uint8_t gpio;
    ulong offAtMs = 0;
    bool enabled = true;

    uint16_t celebrationPattern[8] = {
        60, 80,
        120, 160,
        60, 80,
        120, 0
    };

    uint8_t patternIndex = 0;
    bool patternPlaying = false;
    ulong patternNextAtMs = 0;

public:
    explicit Buzzer(const uint8_t gpio) : gpio(gpio) {}

    void init() const {
        pinMode(gpio, OUTPUT);
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
        if (!enabled) {
            return;
        }

        patternIndex = 0;
        patternPlaying = true;
        patternNextAtMs = millis();
    }

    void loop() {
        const ulong now = millis();

        if (patternPlaying && static_cast<long>(now - patternNextAtMs) >= 0) {
            if (celebrationPattern[patternIndex] == 0) {
                patternPlaying = false;
                digitalWrite(gpio, LOW);
                return;
            }

            const bool isOn = patternIndex % 2 == 0;
            digitalWrite(gpio, isOn ? HIGH : LOW);
            patternNextAtMs = now + celebrationPattern[patternIndex];
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
