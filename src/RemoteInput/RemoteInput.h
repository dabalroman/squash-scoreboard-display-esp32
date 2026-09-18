#ifndef REMOTE_INPUT_H
#define REMOTE_INPUT_H

#include <Arduino.h>

class RemoteInput {
    uint8_t gpio;

    bool canTakeAction = false;
    ulong canBeTriggerAtMs = 0;

    // Measured on the bench (V2, 37 presses): the receiver drives the line HIGH for the
    // whole press and LOW on release, with no dropouts - so a hold is read from the pin
    // level, not from edge timing. Taps run 236-360 ms, deliberate holds 2498-7595 ms.
    static constexpr ulong LONG_PRESS_MS = 2000;      // holds aimed at "3 s" land at 2.5-3.0 s; do not raise
    static constexpr ulong RELEASE_CONFIRM_MS = 40;   // margin only; no dropout was ever observed

    bool deferToRelease = false;
    bool pressActive = false;
    bool pressConsumed = false;
    bool longPressLatched = false;
    bool lowSeen = false;
    ulong pressStartedAtMs = 0;
    ulong lowSinceMs = 0;

    void (*onActionTakenHook)() = nullptr;

    void latch() {
        if (static_cast<long>(canBeTriggerAtMs - millis()) > 0) {
            return;
        }

        canTakeAction = true;
    }

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

    /**
     * Fire this button's short action on release instead of on the first edge. Only the
     * button carrying the long press needs it: otherwise a hold performs the short action
     * on its way to the long one. Costs the press duration (~280 ms) in latency.
     */
    void setDeferToRelease(const bool value) {
        deferToRelease = value;
    }

    void trigger() {
        if (deferToRelease) {
            return;   // poll() owns the latch for this button
        }

        latch();
    }

    /**
     * Level-based press tracking, called every loop pass. Owns the long press for every
     * button and, when deferToRelease is set, the short action too.
     */
    void poll(const ulong nowMs) {
        if (digitalRead(gpio) == HIGH) {
            lowSeen = false;

            if (!pressActive) {
                pressActive = true;
                pressConsumed = false;
                longPressLatched = false;
                pressStartedAtMs = nowMs;
            } else if (!pressConsumed && nowMs - pressStartedAtMs >= LONG_PRESS_MS) {
                // One shot: however long the button is held after this, it fires once.
                longPressLatched = true;
                pressConsumed = true;
            }

            return;
        }

        if (!pressActive) {
            return;
        }

        if (!lowSeen) {
            lowSeen = true;
            lowSinceMs = nowMs;
            return;
        }

        if (nowMs - lowSinceMs < RELEASE_CONFIRM_MS) {
            return;
        }

        // Released. A press that already fired its long action does not also fire the short one.
        if (deferToRelease && !pressConsumed) {
            latch();
        }

        pressActive = false;
        lowSeen = false;
    }

    bool takeActionIfPossible(const ulong preventTriggerForMs = 500) {
        if (canTakeAction) {
            takeAction(preventTriggerForMs);
            return true;
        }

        return false;
    }

    bool takeLongPressIfPossible() {
        if (!longPressLatched) {
            return false;
        }

        longPressLatched = false;
        return true;
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
        longPressLatched = false;
        pressConsumed = true;   // a button still held must not act on the view coming back
    }

    void preventTriggerForMs(const ulong delayMs = 500) {
        canBeTriggerAtMs = millis() + delayMs;
        canTakeAction = false;
        longPressLatched = false;
        pressConsumed = true;
    }
};

#endif //REMOTE_INPUT_H
