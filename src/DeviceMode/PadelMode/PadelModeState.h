#ifndef PADELMODESTATE_H
#define PADELMODESTATE_H

#include <Arduino.h>

enum class PadelModeState : uint8_t {
    Init = 0,
    TournamentChoosePlayers = 1,
    MatchStartGame = 2,
    GamePlaying = 3,
    GameOver = 4,
    TournamentSummary = 5
};

#endif //PADELMODESTATE_H
