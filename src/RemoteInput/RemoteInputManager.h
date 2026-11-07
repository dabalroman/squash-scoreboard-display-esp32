#ifndef REMOTE_INPUT_MANAGER_H
#define REMOTE_INPUT_MANAGER_H

#include "RemoteInput.h"

/**
 * 433Mhz remote button layout
 *
 *   /--------\
 *   | A    B |
 *   |  C  D  |
 *   |        |
 *   |        |
 *   \--------/
 */


class RemoteInputManager {
    bool latchesPassedOn = false;

public:
    RemoteInput buttonA;
    RemoteInput buttonB;
    RemoteInput buttonC;
    RemoteInput buttonD;

    RemoteInputManager(const uint8_t gpioA, const uint8_t gpioB, const uint8_t gpioC, const uint8_t gpioD)
        : buttonA(gpioA), buttonB(gpioB), buttonC(gpioC), buttonD(gpioD) {
        // Only C carries the long press, so only C pays the release latency.
        buttonC.setDeferToRelease(true);
    }

    void handleInput(volatile uint8_t &triggeredGpio) {
        const ulong now = millis();
        buttonA.poll(now);
        buttonB.poll(now);
        buttonC.poll(now);
        buttonD.poll(now);

        const uint8_t gpio = triggeredGpio;  // single read
        triggeredGpio = 0;                   // clear before dispatch; new ISR writes are safe from here

        if (gpio == buttonA.getGPIO()) {
            buttonA.trigger();
        } else if (gpio == buttonB.getGPIO()) {
            buttonB.trigger();
        } else if (gpio == buttonC.getGPIO()) {
            buttonC.trigger();
        } else if (gpio == buttonD.getGPIO()) {
            buttonD.trigger();
        }
    }

    void setOnActionTaken(void (*callback)()) {
        buttonA.setOnActionTaken(callback);
        buttonB.setOnActionTaken(callback);
        buttonC.setOnActionTaken(callback);
        buttonD.setOnActionTaken(callback);
    }

    bool anyActionPending() const {
        return buttonA.hasPendingAction() || buttonB.hasPendingAction()
            || buttonC.hasPendingAction() || buttonD.hasPendingAction();
    }

    /**
     * The next clearLatchesForNewView() keeps the pending presses, so the view coming
     * next takes them. One shot; clearLatches() (an overlay) cancels it.
     */
    void passLatchesToNextView() {
        latchesPassedOn = true;
    }

    // Every sport mode's view swap: drops presses latched in between, unless passed on.
    void clearLatchesForNewView() {
        if (latchesPassedOn) {
            latchesPassedOn = false;
            return;
        }

        clearLatches();
    }

    void clearLatches() {
        latchesPassedOn = false;
        buttonA.clearLatch();
        buttonB.clearLatch();
        buttonC.clearLatch();
        buttonD.clearLatch();
    }

    void preventTriggerForMs(const ulong delayMs = 1000) {
        buttonA.preventTriggerForMs(delayMs);
        buttonB.preventTriggerForMs(delayMs);
        buttonC.preventTriggerForMs(delayMs);
        buttonD.preventTriggerForMs(delayMs);
    }
};

#endif // REMOTE_INPUT_MANAGER_H
