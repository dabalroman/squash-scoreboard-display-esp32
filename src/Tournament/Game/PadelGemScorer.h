#ifndef PADEL_GEM_SCORER_H
#define PADEL_GEM_SCORER_H

#include <Arduino.h>

#include "Tournament/GameSide.h"

/**
 * Tennis-style point token shown for one side within the current gem.
 * Advantage is only ever reported for the side that leads during deuce.
 */
enum class PadelPoint : uint8_t {
    Love = 0,
    Fifteen = 1,
    Thirty = 2,
    Forty = 3,
    Advantage = 4,
};

/**
 * Scores a single padel gem using the advantage system (win by two points).
 *
 * Points are modelled as raw integers (0,1,2,3 = 0/15/30/40), and the gem is
 * won by the first side to reach 4 points with a 2-point lead. This naturally
 * yields deuce/advantage: 3-3 = deuce, 4-3 = advantage, 5-3 = gem, 4-4 = back
 * to deuce.
 *
 * Mirrors Game's tentative/commit model one level down: a rally is uncommitted
 * (and undoable) until commit() is called, so the 4-second undo window behaves
 * exactly like the other sports. commit() returns the gem winner (and resets
 * the ladder) when the committed rally completes the gem, otherwise none.
 */
class PadelGemScorer {
    int8_t pointsA = 0, pointsB = 0;
    int8_t deltaA = 0, deltaB = 0;

    static int8_t clampNonNegative(const int8_t value) {
        return value < 0 ? 0 : value;
    }

    int8_t temporaryPoints(const GameSide side) const {
        if (side == GameSide::a) return clampNonNegative(pointsA + deltaA);
        if (side == GameSide::b) return clampNonNegative(pointsB + deltaB);
        return 0;
    }

public:
    void scoreRally(const GameSide side) {
        if (side == GameSide::a) deltaA++;
        else if (side == GameSide::b) deltaB++;
    }

    void undoRally(const GameSide side) {
        if (side == GameSide::a && (pointsA + deltaA) > 0) deltaA--;
        else if (side == GameSide::b && (pointsB + deltaB) > 0) deltaB--;
    }

    bool hasUncommittedRallies(const GameSide side) const {
        if (side == GameSide::a) return deltaA != 0;
        if (side == GameSide::b) return deltaB != 0;
        return false;
    }

    bool hasUncommittedRallies() const {
        return deltaA != 0 || deltaB != 0;
    }

    bool isEmpty() const {
        return pointsA == 0 && pointsB == 0 && deltaA == 0 && deltaB == 0;
    }

    /**
     * Who would win the gem if the current tentative rallies were committed now
     * (based on the temporary score), or none. Lets the UI capture the deciding
     * point/side before commit() resets the ladder.
     */
    GameSide pendingGemWinner() const {
        const int8_t a = temporaryPoints(GameSide::a);
        const int8_t b = temporaryPoints(GameSide::b);

        if (a >= 4 && (a - b) >= 2) return GameSide::a;
        if (b >= 4 && (b - a) >= 2) return GameSide::b;
        return GameSide::none;
    }

    /**
     * Applies the uncommitted rallies. Returns the gem winner (and resets the
     * ladder for the next gem) if the gem just completed, otherwise none.
     */
    GameSide commit() {
        pointsA = clampNonNegative(pointsA + deltaA);
        pointsB = clampNonNegative(pointsB + deltaB);
        deltaA = 0;
        deltaB = 0;

        if (pointsA >= 4 && (pointsA - pointsB) >= 2) {
            pointsA = 0;
            pointsB = 0;
            return GameSide::a;
        }

        if (pointsB >= 4 && (pointsB - pointsA) >= 2) {
            pointsA = 0;
            pointsB = 0;
            return GameSide::b;
        }

        return GameSide::none;
    }

    /**
     * Display token for the given side, based on temporary (committed + tentative)
     * points so the front display updates immediately, blinking until commit.
     */
    PadelPoint getPoint(const GameSide side) const {
        const int8_t self = temporaryPoints(side);
        const int8_t other = temporaryPoints(side == GameSide::a ? GameSide::b : GameSide::a);

        if (self >= 3 && other >= 3) {
            return self > other ? PadelPoint::Advantage : PadelPoint::Forty;
        }

        if (self >= 3) return PadelPoint::Forty;
        if (self == 2) return PadelPoint::Thirty;
        if (self == 1) return PadelPoint::Fifteen;
        return PadelPoint::Love;
    }
};

#endif //PADEL_GEM_SCORER_H
