#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

class Buzzer {
    uint8_t gpio;
    ulong offAtMs = 0;
    bool enabled = true;

public:
    explicit Buzzer(const uint8_t gpio) : gpio(gpio) {}

    void init() const {
        pinMode(gpio, OUTPUT);
    }

    void setEnabled(const bool value) {
        enabled = value;
    }

    void trigger(const ulong durationMs = 50) {
        if (!enabled) {
            return;
        }
        
        digitalWrite(gpio, HIGH);
        offAtMs = millis() + durationMs;
    }

    void loop() {
        if (offAtMs > 0 && millis() >= offAtMs) {
            digitalWrite(gpio, LOW);
            offAtMs = 0;
        }
    }
};

#endif //BUZZER_H
