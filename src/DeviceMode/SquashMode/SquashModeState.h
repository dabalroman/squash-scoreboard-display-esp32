#ifndef SQUASHMODESTATE_H
#define SQUASHMODESTATE_H

#include <Arduino.h>

enum class SquashModeState : uint8_t {
    Init = 0,
    TournamentChoosePlayers = 1,
    MatchChoosePlayers = 2,
    MatchOn = 3,
    MatchOver = 4,
    TournamentSummary = 5
};

#endif //SQUASHMODESTATE_H
