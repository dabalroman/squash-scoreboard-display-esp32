#ifndef MATCH_RESULT
#define MATCH_RESULT

#include <Arduino.h>

struct MatchResult {
    uint8_t playerAId;
    uint8_t playerAScore;
    uint8_t playerBId;
    uint8_t playerBScore;
    uint8_t winnerPlayerId;

    /** Games (sets, in padel) won by this player in the match. */
    uint8_t scoreOf(const uint8_t playerId) const {
        if (playerId == playerAId) return playerAScore;
        if (playerId == playerBId) return playerBScore;
        return 0;
    }
};

#endif //MATCH_RESULT
