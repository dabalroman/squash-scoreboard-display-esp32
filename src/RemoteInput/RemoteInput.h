#ifndef REMOTE_INPUT_H
#define REMOTE_INPUT_H

#include <Arduino.h>

class RemoteInput {
    uint8_t gpio;

    bool canTakeAction = false;

    ulong triggeredAtMs = 0;
    ulong canBeTriggerAtMs = 0;

    void takeAction(const ulong preventTriggerForMs = 500) {
        canTakeAction = false;
        canBeTriggerAtMs = millis() + preventTriggerForMs;
    }

public:
    explicit RemoteInput(const uint8_t gpio) : gpio(gpio) {
    }

    uint8_t getGPIO() const {
        return gpio;
    }

    void trigger() {
        if (canBeTriggerAtMs > millis()) {
            return;
        }

        canTakeAction = true;
        triggeredAtMs = millis();
    }

    bool takeActionIfPossible(const ulong preventTriggerForMs = 500) {
        if (canTakeAction && canBeTriggerAtMs <= millis()) {
            takeAction(preventTriggerForMs);
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
