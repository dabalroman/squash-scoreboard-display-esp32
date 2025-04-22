#ifndef MATCHROUND_H
#define MATCHROUND_H

#include <Arduino.h>

#include "Rules/Rules.h"

class MatchRound {
    int8_t scoreA = 0, scoreB = 0;
    int8_t deltaA = 0, deltaB = 0;
    MatchSide winner = MatchSide::none;

public:
    uint8_t id;

    explicit MatchRound(const uint8_t id) : id(id) {
    }

    void scorePointA() {
        if (winner != MatchSide::none) {
            return;
        }

        deltaA++;
    }

    void losePointA() {
        if (winner != MatchSide::none) {
            return;
        }

        deltaA--;
    }

    void scorePointB() {
        if (winner != MatchSide::none) {
            return;
        }

        deltaB++;
    }

    void losePointB() {
        if (winner != MatchSide::none) {
            return;
        }

        deltaB--;
    }

    void rollback() {
        deltaA = 0;
        deltaB = 0;
    }

    void commit(const Rules &rules) {
        if (winner != MatchSide::none) {
            return;
        }

        scoreA += deltaA;
        scoreB += deltaB;

        if (scoreA < 0) {
            scoreA = 0;
        }

        if (scoreB < 0) {
            scoreB = 0;
        }

        winner = rules.checkWinner(scoreA, scoreB);
    }
};

#endif //MATCHROUND_H
