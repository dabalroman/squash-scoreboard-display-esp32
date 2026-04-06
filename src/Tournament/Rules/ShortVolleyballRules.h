#ifndef SHORTVOLLEYBALLRULES_H
#define SHORTVOLLEYBALLRULES_H

#include <Arduino.h>

#include "Rules.h"
#include "../GameSide.h"

class ShortVolleyballRules final : public Rules {
    GameSide checkWinner(const int8_t scoreA, const int8_t scoreB) const override {
        if (scoreA >= 15 && (scoreA - scoreB) >= 2) return GameSide::a;
        if (scoreB >= 15 && (scoreB - scoreA) >= 2) return GameSide::b;
        return GameSide::none;
    }
};

#endif //SHORTVOLLEYBALLRULES_H
