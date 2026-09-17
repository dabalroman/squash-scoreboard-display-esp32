#ifndef LED_BAR_MODE
#define LED_BAR_MODE

#include <Arduino.h>

enum class LedBarMode: uint8_t {
    state = 0,
    celebration = 1,
};

#endif //LED_BAR_MODE
