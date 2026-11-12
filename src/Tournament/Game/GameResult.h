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
    // Non-zero, unique within the tournament. RAM only; on the wire only in tablet mode (spec 20).
    uint32_t id;
    // Entered by a client (Match::recordGame), not played on the board.
    bool offline;
    // The only field ever changed after append, and only on an offline game.
    bool removed;
    // Tablet mode: awaiting the tablet's accept. Counted in the court's tally, not listed.
    bool pending;
    // Added by this board (tracked or offline), not only mirrored from the tablet.
    bool local;
    // Tablet mode: the tablet has stored it (ACK_OUTBOX or its id in a mirror).
    bool acked;
    // Tablet mode: the outbox seq (1, 2, ...); 0 = not in the outbox.
    uint16_t outboxSeq;
};

#endif //GAME_RESULT_H
