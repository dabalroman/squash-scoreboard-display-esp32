#ifndef DEVICEMODE_H
#define DEVICEMODE_H

#include <Arduino.h>

enum class DeviceModeState: uint8_t {
    Booting = 0,
    ModeSwitchingMode = 1,
    ConfigMode = 2,
    SquashMode = 3,
    VolleyballMode = 4,
    ShortVolleyballMode = 5,
    // Clock = 2,
    // Development = 3
};

#endif //DEVICEMODE_H
