#ifndef GAME_RESULT_H
#define GAME_RESULT_H

#include <Arduino.h>

#include "GameSide.h"

struct GameResult {
    uint8_t playerAId;
    uint8_t playerBId;
    GameSide winner;
};

#endif //GAME_RESULT_H
