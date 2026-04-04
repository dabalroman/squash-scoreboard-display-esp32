#ifndef MATCH_SIDE_ENUM
#define MATCH_SIDE_ENUM

#include <Arduino.h>

enum class MatchSide: uint8_t {
    none = 0,
    a = 1,
    b = 2,
};

#endif //MATCH_SIDE_ENUM
