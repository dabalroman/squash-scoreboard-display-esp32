#ifndef MATCH_RESULT
#define MATCH_RESULT

#include <Arduino.h>

#include "../GameSide.h"

struct MatchResult {
    uint8_t playerAId;
    uint8_t playerAScore;
    uint8_t playerBId;
    uint8_t playerBScore;
    GameSide winner;
};

#endif //MATCH_RESULT
