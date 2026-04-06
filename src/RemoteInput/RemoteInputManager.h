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
public:
    RemoteInput buttonA;
    RemoteInput buttonB;
    RemoteInput buttonC;
    RemoteInput buttonD;

    RemoteInputManager(const uint8_t gpioA, const uint8_t gpioB, const uint8_t gpioC, const uint8_t gpioD)
        : buttonA(gpioA), buttonB(gpioB), buttonC(gpioC), buttonD(gpioD) {
    }

    void handleInput(volatile uint8_t &triggeredGpio) {
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

    void preventTriggerForMs(const ulong delayMs = 1000) {
        buttonA.preventTriggerForMs(delayMs);
        buttonB.preventTriggerForMs(delayMs);
        buttonC.preventTriggerForMs(delayMs);
        buttonD.preventTriggerForMs(delayMs);
    }
};

#endif // REMOTE_INPUT_MANAGER_H
