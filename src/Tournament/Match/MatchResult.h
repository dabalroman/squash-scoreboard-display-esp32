#ifndef MATCH_RESULT
#define MATCH_RESULT

#include <Arduino.h>

struct MatchResult {
    uint8_t playerAId;
    uint8_t playerAScore;
    uint8_t playerBId;
    uint8_t playerBScore;
    uint8_t winnerPlayerId;
};

#endif //MATCH_RESULT
