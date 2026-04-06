#ifndef SQUASH_SCOREBOARD_DISPLAY_ESP32_MATCHHEAD2HEADSCORE_H
#define SQUASH_SCOREBOARD_DISPLAY_ESP32_MATCHHEAD2HEADSCORE_H

#include <Arduino.h>

#include "../GameSide.h"

struct MatchResult {
    uint8_t playerAId;
    uint8_t playerAScore;
    uint8_t playerBId;
    uint8_t playerBScore;
    GameSide winner;
};

#endif //SQUASH_SCOREBOARD_DISPLAY_ESP32_MATCHHEAD2HEADSCORE_H
