#ifndef VOLLEYBALLRULES_H
#define VOLLEYBALLRULES_H

#include <Arduino.h>

#include "Rules.h"
#include "../MatchSideEnum.h"

class VolleyballRules final : public Rules {
    MatchSide checkWinner(const int8_t scoreA, const int8_t scoreB) const override {
        if (scoreA >= 25 && (scoreA - scoreB) >= 2) return MatchSide::a;
        if (scoreB >= 25 && (scoreB - scoreA) >= 2) return MatchSide::b;
        return MatchSide::none;
    }
};

#endif //VOLLEYBALLRULES_H
