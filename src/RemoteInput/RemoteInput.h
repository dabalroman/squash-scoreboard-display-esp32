#ifndef REMOTE_INPUT_H
#define REMOTE_INPUT_H

#include <Arduino.h>

class RemoteInput {
    uint8_t gpio;

    bool canTakeAction = false;
    ulong canBeTriggerAtMs = 0;

    void (*onActionTakenHook)() = nullptr;

    void takeAction(const ulong preventTriggerForMs = 500) {
        canTakeAction = false;
        canBeTriggerAtMs = millis() + preventTriggerForMs;

        if (onActionTakenHook) {
            onActionTakenHook();
        }
    }

public:
    explicit RemoteInput(const uint8_t gpio) : gpio(gpio) {
    }

    uint8_t getGPIO() const {
        return gpio;
    }

    void trigger() {
        if (static_cast<long>(canBeTriggerAtMs - millis()) > 0) {
            return;
        }

        canTakeAction = true;
    }

    bool takeActionIfPossible(const ulong preventTriggerForMs = 500) {
        if (canTakeAction) {
            takeAction(preventTriggerForMs);
            return true;
        }

        return false;
    }

    void setOnActionTaken(void (*callback)()) {
        onActionTakenHook = callback;
    }

    /**
     * Drop a press that is latched but not yet consumed, without touching the
     * debounce window. Used after a device-level overlay, so buttons pushed while
     * the screen was taken over do not act on the view that comes back.
     */
    void clearLatch() {
        canTakeAction = false;
    }

    void preventTriggerForMs(const ulong delayMs = 500) {
        canBeTriggerAtMs = millis() + delayMs;
        canTakeAction = false;
    }
};

#endif //REMOTE_INPUT_H
