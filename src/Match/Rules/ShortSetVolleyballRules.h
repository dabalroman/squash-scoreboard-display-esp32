#ifndef SHORTSETVOLLEYBALLRULES_H
#define SHORTSETVOLLEYBALLRULES_H

#include <Arduino.h>

#include "Rules.h"
#include "../MatchSideEnum.h"

class ShortSetVolleyballRules final : public Rules {
    MatchSide checkWinner(const int8_t scoreA, const int8_t scoreB) const override {
        if (scoreA >= 15 && (scoreA - scoreB) >= 2) return MatchSide::a;
        if (scoreB >= 15 && (scoreB - scoreA) >= 2) return MatchSide::b;
        return MatchSide::none;
    }
};

#endif //SHORTSETVOLLEYBALLRULES_H
