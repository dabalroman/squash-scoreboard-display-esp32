#ifndef SAFERESTART_H
#define SAFERESTART_H

#include <Arduino.h>
#include <driver/gpio.h>

#include "Board.h"

// On reset every pad returns to a floating input, and on V2 the floating MOSFET gate lets the
// buzzer sound. A plain LOW is lost at reset, so the pad is latched in the RTC domain instead -
// gpio_hold_en() survives a software reset and is released by Buzzer::init() on the next boot.
inline void safeRestart() {
    pinMode(Board::BUZZER, OUTPUT);
    digitalWrite(Board::BUZZER, LOW);
    gpio_hold_en(static_cast<gpio_num_t>(Board::BUZZER));

    ESP.restart();
}

#endif //SAFERESTART_H
