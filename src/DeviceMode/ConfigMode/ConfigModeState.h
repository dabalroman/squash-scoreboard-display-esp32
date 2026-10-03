#ifndef CONFIG_MODE_STATE_H
#define CONFIG_MODE_STATE_H

#include <stdint.h>

enum class ConfigModeState : uint8_t {
    Menu,
    Garmin,
    GarminPairing,
};

#endif //CONFIG_MODE_STATE_H
