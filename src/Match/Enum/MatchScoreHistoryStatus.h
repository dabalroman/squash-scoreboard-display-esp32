#ifndef MATCH_SCORE_HISTORY_STATUS
#define MATCH_SCORE_HISTORY_STATUS

#include <Arduino.h>

enum class MatchScoreHistoryStatus: uint8_t {
    committed = 0,
    lost = 1,
    scored = 2
};

#endif //MATCH_SCORE_HISTORY_STATUS
