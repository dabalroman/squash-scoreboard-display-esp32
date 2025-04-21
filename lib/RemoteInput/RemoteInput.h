#ifndef REMOTE_INPUT_H
#define REMOTE_INPUT_H

#include <Arduino.h>
#define LONG_TOUCH_MS 1000

class RemoteInput {
    uint8_t gpio;
    ulong triggeredAt = 0;
    ulong canBeTriggerAt = 0;
    bool triggered = false;
    bool isActionTaken = false;

    void takeAction() {
        isActionTaken = true;
        this->canBeTriggerAt = millis() + 50;
    }

public:
    explicit RemoteInput(const uint8_t gpio) : gpio(gpio) {}

    void trigger() {
        this->triggered = true;
        this->isActionTaken = false;
        this->triggeredAt = millis();
    }

    void preventAccidentalActionFor(const ulong delay = 500) {
        this->canBeTriggerAt = millis() + delay;
    }

    uint8_t getGPIO() const {
        return gpio;
    }

    bool takeActionIfPossible() {
        if (triggered && !isActionTaken && canBeTriggerAt <= millis()) {
            this->takeAction();
            return true;
        }

        return false;
    }

    bool isTouched() const {
        return triggered;
    }
};

#endif //REMOTE_INPUT_H
