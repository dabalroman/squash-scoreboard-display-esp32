#ifndef SQUASHMODESTATE_H
#define SQUASHMODESTATE_H

#include <Arduino.h>

enum class SquashModeState : uint8_t {
    Init = 0,
    ChoosePlayers = 1,
    Match = 2,
    MatchOver = 3,
};

#endif //SQUASHMODESTATE_H
