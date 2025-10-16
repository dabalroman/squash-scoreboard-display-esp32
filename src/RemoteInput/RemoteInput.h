#ifndef REMOTE_INPUT_H
#define REMOTE_INPUT_H

#include <Arduino.h>
constexpr uint8_t minimumTriggerDelayMs = 200;

class RemoteInput {
    uint8_t gpio;

    bool canTakeAction = false;

    ulong triggeredAtMs = 0;
    ulong canBeTriggerAtMs = 0;

    void takeAction() {
        canTakeAction = false;
        canBeTriggerAtMs = millis() + minimumTriggerDelayMs;
    }

public:
    explicit RemoteInput(const uint8_t gpio) : gpio(gpio) {
    }

    uint8_t getGPIO() const {
        return gpio;
    }

    void trigger() {
        canTakeAction = true;
        triggeredAtMs = millis();
    }

    bool takeActionIfPossible() {
        if (canTakeAction && canBeTriggerAtMs <= millis()) {
            takeAction();
            return true;
        }

        return false;
    }

    void preventTriggerForMs(const ulong delayMs = 500) {
        canBeTriggerAtMs = millis() + delayMs;
        canTakeAction = false;
    }
};

#endif //REMOTE_INPUT_H
