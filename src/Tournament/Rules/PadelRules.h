#ifndef PADELRULES_H
#define PADELRULES_H

#include <Arduino.h>

#include "Rules.h"
#include "../GameSide.h"

/**
 * Padel set rules. Operates on GEMS won within a set (a "game" in engine terms).
 * A set is won by the first side to reach GEMS_PER_SET gems with a 2-gem lead.
 * All-square at GEMS_PER_SET the set goes to a tiebreak, which can only ever end
 * one gem above GEMS_PER_SET - so any score above it is a won tiebreak (7-6).
 *
 * The within-gem tennis ladder (0/15/30/40/advantage) lives one level below this
 * abstraction, in PadelGemScorer; the tiebreak is that same scorer counting
 * numerically to 7 instead.
 */
class PadelRules final : public Rules {
public:
    static constexpr uint8_t GEMS_PER_SET = 6;

    /** All-square at GEMS_PER_SET: the gem being played is the tiebreak. */
    static bool isTiebreakScore(const uint8_t gemsA, const uint8_t gemsB) {
        return gemsA == GEMS_PER_SET && gemsB == GEMS_PER_SET;
    }

private:
    GameSide checkWinner(const int8_t gemsA, const int8_t gemsB) const override {
        if (gemsA > GEMS_PER_SET || (gemsA >= GEMS_PER_SET && (gemsA - gemsB) >= 2)) return GameSide::a;
        if (gemsB > GEMS_PER_SET || (gemsB >= GEMS_PER_SET && (gemsB - gemsA) >= 2)) return GameSide::b;
        return GameSide::none;
    }

    size_t historyReserve() const override {
        return 16;
    }
};

#endif //PADELRULES_H
