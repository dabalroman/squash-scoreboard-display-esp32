#ifndef VOLLEYBALLRULES_H
#define VOLLEYBALLRULES_H

#include <Arduino.h>

#include "Rules.h"
#include "../GameSide.h"

class VolleyballRules final : public Rules {
    GameSide checkWinner(const int8_t scoreA, const int8_t scoreB) const override {
        if (scoreA >= 25 && (scoreA - scoreB) >= 2) return GameSide::a;
        if (scoreB >= 25 && (scoreB - scoreA) >= 2) return GameSide::b;
        return GameSide::none;
    }
};

#endif //VOLLEYBALLRULES_H
