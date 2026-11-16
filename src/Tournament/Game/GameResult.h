#ifndef GAME_RESULT_H
#define GAME_RESULT_H

#include <Arduino.h>

// A finished game as the views show it: profile ids in the match's own A/B order.
struct GameResult {
    uint8_t playerAId;
    uint8_t playerAScore;
    uint8_t playerBId;
    uint8_t playerBScore;
    uint8_t winnerPlayerId;
};

#endif //GAME_RESULT_H
