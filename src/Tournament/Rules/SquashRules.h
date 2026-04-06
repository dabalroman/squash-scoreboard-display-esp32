#ifndef SQUASHRULES_H
#define SQUASHRULES_H

#include <Arduino.h>

#include "Rules.h"
#include "../GameSide.h"

class SquashRules final : public Rules {
    GameSide checkWinner(const int8_t scoreA, const int8_t scoreB) const override {
        if (scoreA >= 11 && (scoreA - scoreB) >= 2) return GameSide::a;
        if (scoreB >= 11 && (scoreB - scoreA) >= 2) return GameSide::b;
        return GameSide::none;
    }
};

#endif //SQUASHRULES_H
