#ifndef GAME_SIDE_ENUM
#define GAME_SIDE_ENUM

#include <Arduino.h>

enum class GameSide: uint8_t {
    none = 0,
    a = 1,
    b = 2,
};

#endif //GAME_SIDE_ENUM
