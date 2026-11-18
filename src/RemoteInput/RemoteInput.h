#ifndef REMOTE_INPUT_H
#define REMOTE_INPUT_H

#include <Arduino.h>

#ifdef INPUT_DIAG
#include "RemoteDevelopmentService/LoggerHelper.h"
#endif

class RemoteInput {
    uint8_t gpio;
#ifdef INPUT_DIAG
    char diagName = '?';
#endif

    bool canTakeAction = false;
    ulong canBeTriggerAtMs = 0;

    static constexpr ulong LONG_PRESS_MS = 2000;
    // A fob press from ~6 m can reach the receiver twice, up to ~380 ms apart (#630): one
    // window for menus and scoring alike, just above that.
    static constexpr ulong REPEAT_BLOCK_MS = 400;
    static constexpr ulong RELEASE_CONFIRM_MS = 40;

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
#ifdef INPUT_DIAG
            printLn("Input diag: %c latch refused (debounce)", diagName);
#endif
            return;
        }

#ifdef INPUT_DIAG
        printLn("Input diag: %c latched%s", diagName, canTakeAction ? " (already pending)" : "");
#endif
        canTakeAction = true;
    }

    void takeAction(const ulong preventTriggerForMs = REPEAT_BLOCK_MS) {
#ifdef INPUT_DIAG
        printLn("Input diag: %c taken", diagName);
#endif
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

#ifdef INPUT_DIAG
    void setDiagName(const char name) {
        diagName = name;
    }
#endif

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
#ifdef INPUT_DIAG
                printLn("Input diag: %c long press", diagName);
#endif
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

#ifdef INPUT_DIAG
        // Real taps measured 236-360 ms: a far shorter HIGH is noise, not a person.
        printLn("Input diag: %c high %lu ms%s", diagName, lowSinceMs - pressStartedAtMs,
                lowSinceMs - pressStartedAtMs < 100 ? " SHORT" : "");
#endif

        // Released. A press that already fired its long action does not also fire the short one.
        if (deferToRelease && !pressConsumed) {
            latch();
        }

        pressActive = false;
        lowSeen = false;
    }

    bool takeActionIfPossible(const ulong preventTriggerForMs = REPEAT_BLOCK_MS) {
        if (canTakeAction) {
            takeAction(preventTriggerForMs);
            return true;
        }

        return false;
    }

    // Peek without taking: no debounce window, no buzzer tick.
    bool hasPendingAction() const {
        return canTakeAction;
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
