#ifndef GAME_SCORE_HISTORY_STATUS
#define GAME_SCORE_HISTORY_STATUS

#include <Arduino.h>

enum class GameScoreHistoryStatus: uint8_t {
    committed = 0,
    lost = 1,
    scored = 2
};

#endif //GAME_SCORE_HISTORY_STATUS
