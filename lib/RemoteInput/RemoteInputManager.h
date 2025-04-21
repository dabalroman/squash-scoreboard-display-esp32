#ifndef REMOTE_INPUT_MANAGER_H
#define REMOTE_INPUT_MANAGER_H

#include "RemoteInput.h"

class RemoteInputManager {
public:
    RemoteInput buttonA;
    RemoteInput buttonB;
    RemoteInput buttonC;
    RemoteInput buttonD;

    RemoteInputManager(const uint8_t gpioA, const uint8_t gpioB, const uint8_t gpioC, const uint8_t gpioD)
        : buttonA(gpioA), buttonB(gpioB), buttonC(gpioC), buttonD(gpioD) {
    }

    void trigger(const uint8_t gpio) {
        if (gpio == buttonA.getGPIO()) buttonA.trigger();
        else if (gpio == buttonB.getGPIO()) buttonB.trigger();
        else if (gpio == buttonC.getGPIO()) buttonC.trigger();
        else if (gpio == buttonD.getGPIO()) buttonD.trigger();
    }

    void preventAccidentalActionFor(const ulong delay = 500) {
        buttonA.preventAccidentalActionFor(delay);
        buttonB.preventAccidentalActionFor(delay);
        buttonC.preventAccidentalActionFor(delay);
        buttonD.preventAccidentalActionFor(delay);
    }
};

#endif // REMOTE_INPUT_MANAGER_H
