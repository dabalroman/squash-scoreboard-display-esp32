#ifndef SQUASHMODESTATE_H
#define SQUASHMODESTATE_H

#include <Arduino.h>

enum class SquashModeState : uint8_t {
    Init = 0,
    TournamentChoosePlayers = 1,
    MatchStartGame = 2,
    GamePlaying = 3,
    GameOver = 4,
    TournamentSummary = 5
};

#endif //SQUASHMODESTATE_H
