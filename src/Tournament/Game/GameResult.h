#ifndef GAME_RESULT_H
#define GAME_RESULT_H

#include <Arduino.h>

// Trailing fields stay last and without initializers: callers brace-init the first five (C++11 aggregate).
struct GameResult {
    uint8_t playerAId;
    uint8_t playerAScore;
    uint8_t playerBId;
    uint8_t playerBScore;
    uint8_t winnerPlayerId;
    // Non-zero, unique within the tournament. RAM only, on no frame yet.
    uint32_t id;
    // Entered by a client (Match::recordGame), not played on the board.
    bool offline;
    // The only field ever changed after append, and only on an offline game.
    bool removed;
};

#endif //GAME_RESULT_H
