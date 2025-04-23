#ifndef MATCHROUND_H
#define MATCHROUND_H

#include <Arduino.h>

#include "Rules/Rules.h"

class MatchRound {
    uint8_t id;

    int8_t scoreA = 0, scoreB = 0;
    int8_t deltaA = 0, deltaB = 0;
    MatchSide winner = MatchSide::none;
    Rules &rules;

public:
    explicit MatchRound(const uint8_t id, Rules &rules) : id(id), rules(rules) {
    }

    uint8_t getId() const {
        return id;
    }

    int8_t getScoreA() const {
        return scoreA;
    }

    int8_t getScoreB() const {
        return scoreB;
    }

    MatchSide getWinner() const {
        return winner;
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

    MatchSide commit() {
        if (winner != MatchSide::none) {
            return winner;
        }

        scoreA += deltaA;
        scoreB += deltaB;

        deltaA = 0;
        deltaB = 0;

        if (scoreA < 0) {
            scoreA = 0;
        }

        if (scoreB < 0) {
            scoreB = 0;
        }

        winner = rules.checkWinner(scoreA, scoreB);
        return winner;
    }
};

#endif //MATCHROUND_H
