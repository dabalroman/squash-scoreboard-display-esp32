#ifndef DEVICEMODE_H
#define DEVICEMODE_H

#include <Arduino.h>

enum class DeviceModeState: uint8_t {
    Booting = 0,
    SquashMode = 1,
    Clock = 2,
    Development = 3
};

#endif //DEVICEMODE_H
