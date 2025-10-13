#ifndef SQUASHRULES_H
#define SQUASHRULES_H

#include <Arduino.h>

#include "Rules.h"
#include "../MatchSideEnum.h"

class SquashRules final : public Rules {
    MatchSide checkWinner(const int8_t scoreA, const int8_t scoreB) const override {
        if (scoreA >= 11 && (scoreA - scoreB) >= 2) return MatchSide::a;
        if (scoreB >= 11 && (scoreB - scoreA) >= 2) return MatchSide::b;
        return MatchSide::none;
    }
};

#endif //SQUASHRULES_H
