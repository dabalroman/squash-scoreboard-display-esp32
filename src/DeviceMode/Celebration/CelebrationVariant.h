#ifndef CELEBRATION_VARIANT_H
#define CELEBRATION_VARIANT_H

#include <Arduino.h>
#include "Tournament/Match/Match.h"
#include "Tournament/Game/GameResult.h"

enum class CelebrationVariant : uint8_t {
    Normal,
    Bajgiel   ///< the loser scored 0: squash 11-0, volleyball 25-0/15-0, padel set 6-0
};

// Sport-agnostic on purpose: variants read the result, never the Rules.
inline CelebrationVariant selectCelebrationVariant(const Match &, const GameResult &result) {
    return (result.playerAScore == 0 || result.playerBScore == 0) ? CelebrationVariant::Bajgiel : CelebrationVariant::Normal;
}

#endif //CELEBRATION_VARIANT_H
