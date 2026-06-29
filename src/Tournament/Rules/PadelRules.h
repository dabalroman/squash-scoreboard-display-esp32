#ifndef PADELRULES_H
#define PADELRULES_H

#include <Arduino.h>

#include "Rules.h"
#include "../GameSide.h"

/**
 * Padel set rules. Operates on GEMS won within a set (a "game" in engine terms).
 * A set is won by the first side to reach 6 gems with a 2-gem lead
 * (6-6 continues to 7-5, 8-6, ... — advantage games, no tie-break).
 *
 * The within-gem tennis ladder (0/15/30/40/advantage) lives one level below
 * this abstraction, in PadelGemScorer.
 */
class PadelRules final : public Rules {
    GameSide checkWinner(const int8_t gemsA, const int8_t gemsB) const override {
        if (gemsA >= 6 && (gemsA - gemsB) >= 2) return GameSide::a;
        if (gemsB >= 6 && (gemsB - gemsA) >= 2) return GameSide::b;
        return GameSide::none;
    }
};

#endif //PADELRULES_H
