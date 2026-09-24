#ifndef LED_TARGET_H
#define LED_TARGET_H

#include <stdint.h>

/**
 * Which display elements a layer may touch. LedDisplay builds the slot -> element
 * map from its own components' segment tables (markSlots), so no slot list is
 * kept by hand; an element with no LEDs on a board (V2 colon, V2 bar) maps to no
 * slots.
 */
namespace LedTarget {
    enum : uint16_t {
        DigitA = 1u << 0,
        DigitB = 1u << 1,
        DigitC = 1u << 2,
        DigitD = 1u << 3,
        Colon = 1u << 4,
        IndicatorA = 1u << 5,
        IndicatorB = 1u << 6,
        BorderTop = 1u << 7,
        BorderBottom = 1u << 8,
        Bar = 1u << 9,

        LeftScore = DigitA | DigitB,
        RightScore = DigitC | DigitD,
        Front = LeftScore | RightScore | Colon | BorderTop | BorderBottom | Bar,
        All = Front | IndicatorA | IndicatorB,
    };
}

/** AllSlots: every LED of the targeted elements. LitOnly: only those the base lit this frame. */
enum class LayerMask : uint8_t { AllSlots, LitOnly };

#endif //LED_TARGET_H
