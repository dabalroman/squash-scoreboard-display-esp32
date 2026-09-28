#ifndef CELEBRATION_VARIANT_H
#define CELEBRATION_VARIANT_H

#include <Arduino.h>
#include "Tournament/Match/Match.h"
#include "Tournament/Game/GameResult.h"

enum class CelebrationVariant : uint8_t {
    Normal
};

// Sport-agnostic on purpose: variants read the result, never the Rules.
inline CelebrationVariant selectCelebrationVariant(const Match &, const GameResult &) {
    return CelebrationVariant::Normal;
}

#endif //CELEBRATION_VARIANT_H
