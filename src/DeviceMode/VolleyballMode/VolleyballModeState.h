#ifndef VOLLEYBALL_MODESTATE_H
#define VOLLEYBALL_MODESTATE_H

#include <Arduino.h>

enum class VolleyballModeState : uint8_t {
    Init = 0,
    TournamentChoosePlayers = 1,
    MatchStartGame = 2,
    GamePlaying = 3,
    GameOver = 4,
    TournamentSummary = 5
};

#endif //VOLLEYBALL_MODESTATE_H
