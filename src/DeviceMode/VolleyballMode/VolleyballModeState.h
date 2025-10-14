#ifndef VOLLEYBALL_MODESTATE_H
#define VOLLEYBALL_MODESTATE_H

#include <Arduino.h>

enum class VolleyballModeState : uint8_t {
    Init = 0,
    TournamentChoosePlayers = 1,
    MatchChoosePlayers = 2,
    MatchPlaying = 3,
    MatchOver = 4,
    TournamentSummary = 5
};

#endif //VOLLEYBALL_MODESTATE_H
