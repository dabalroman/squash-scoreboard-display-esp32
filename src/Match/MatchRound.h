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

    uint8_t getRealScore(const MatchSide side) const {
        if (side == MatchSide::a) {
            return static_cast<uint8_t>(scoreA);
        }

        if (side == MatchSide::b) {
            return static_cast<uint8_t>(scoreB);
        }

        return 0;
    }

    uint8_t getTemporaryScore(const MatchSide side) const {
        int8_t score = 0;

        if (side == MatchSide::a) {
            score = scoreA + deltaA;
        } else if (side == MatchSide::b) {
            score = scoreB + deltaB;
        }

        return static_cast<uint8_t>(score < 0 ? 0 : score);
    }

    MatchSide getWinner() const {
        return winner;
    }

    void scorePoint(const MatchSide side) {
        if (winner != MatchSide::none) {
            return;
        }

        if (side == MatchSide::a) {
            deltaA++;
        } else if (side == MatchSide::b) {
            deltaB++;
        }
    }

    void losePoint(const MatchSide side) {
        if (winner != MatchSide::none) {
            return;
        }

        if (side == MatchSide::a && scoreA + deltaA > 0) {
            deltaA--;
        } else if (side == MatchSide::b && scoreB + deltaB > 0) {
            deltaB--;
        }
    }

    bool hasUncommitedPoints(const MatchSide side) const {
        if (side == MatchSide::a) {
            return deltaA != 0;
        }

        if (side == MatchSide::b) {
            return deltaB != 0;
        }

        return false;
    }

    bool hasUncommitedPoints() const {
        return hasUncommitedPoints(MatchSide::a) || hasUncommitedPoints(MatchSide::b);
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
