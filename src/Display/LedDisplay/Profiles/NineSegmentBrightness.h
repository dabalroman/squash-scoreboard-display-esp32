#ifndef NINE_SEGMENT_BRIGHTNESS_H
#define NINE_SEGMENT_BRIGHTNESS_H

#include <FastLED.h>

#include "Board.h"

/**
 * Per-segment brightness compensation, V2 only (task #43).
 *
 * WHY: a nine-segment module's segments differ ~2.3x in lit area per die, so
 * equal PWM makes the small segments glare next to the big ones. The
 * left/right split (e.g. bottom-left 187 vs bottom-right 383 mm^2/die) is a
 * real physical asymmetry of the panel - do not "fix" it away.
 *
 * HOW: scale = min(1.0, TOLERANCE * mm2PerDie / REFERENCE), linear in area,
 * dimming-only (a factor never exceeds 255). mm2PerDie is each segment's
 * measured area divided by its own die count (below). REFERENCE = 383.0
 * mm^2/die is bottom-right's value, the *largest* of the set, because
 * compensation can only attenuate - any smaller reference would ask some
 * segment to brighten past 255, which nscale8() cannot do. TOLERANCE = 1.2 is
 * the accepted 20% spread in light per mm^2 across the panel; it is the one
 * knob to retune from the bench. This is linear, not perceptual/gamma - a
 * deliberate simplification (user decision 2026-09-23).
 *
 * Measured (dies, total mm^2, mm^2/die) and the resulting 8-bit factor
 * (round(255 * min(1, 1.2 * mm2PerDie / 383.0))):
 *   bottom        3 dies, 1103 mm^2 (367.7/die) -> 255 (clamped)
 *   bottom-left   2 dies,  374 mm^2 (187.0/die) -> 149
 *   bottom-right  2 dies,  766 mm^2 (383.0/die) -> 255 (clamped - the reference segment)
 *   center        2 dies,  349 mm^2 (174.5/die) -> 139
 *   mid-left      2 dies,  331 mm^2 (165.5/die) -> 132
 *   mid-right     2 dies,  331 mm^2 (165.5/die) -> 132
 *   top           3 dies, 1044 mm^2 (348.0/die) -> 255 (clamped)
 *   upper-left    2 dies,  345 mm^2 (172.5/die) -> 138
 *   top-right     2 dies,  693 mm^2 (346.5/die) -> 255 (clamped)
 *   border        2 dies,  473 mm^2 (236.5/die) -> 189 (same formula, no special case)
 * Back indicators (slots 4, 9) and the dead chain positions (12, 28, 44, 60)
 * are never front-facing / never lit - left at 255 (identity).
 *
 * Parallel dies are full WS2812Bs sharing one data slot, so
 * each die still emits at full brightness - the scale is per SLOT (one factor
 * per addressable pixel, applied once), not per die; die count only feeds the
 * mm^2-per-die measurement above (total area / dies), never anything at runtime.
 *
 * POWER: this replaces V1's blanket 0.8 brightness limit as V2's power guard
 * (see Board::GLOBAL_BRIGHTNESS_SCALE). Worst case - four "8" glyphs
 * (4 x 15.4 = 61.7) + border (5.9) + both indicators (2.0) die-equivalents at
 * brightness level 8 - sums to 69.6, against 72 (0.8 x 90) under the old flat
 * limit, so peak draw does not rise. Lighter content (e.g. "1111", every lit
 * segment clamped to full) draws more than the old 0.8 scheme on V2, but
 * stays under that 69.6 <= 72 bound - accepted.
 *
 * Factors below are literals, not computed at build time: test_v2_glyphs
 * independently recomputes them from the areas above and asserts they match.
 */
struct NineSegmentBrightness {
    /**
     * Slot relative to a digit module's own base (0..15, see the layout comment
     * atop NineSegmentProfile.h). Slot 2 is that module's dead chain position -
     * never written, so its factor is never used; 255 is a safe placeholder.
     */
    static uint8_t moduleFactor(const uint16_t moduleSlot) {
        constexpr uint8_t FACTORS[Board::PIXELS_PER_MODULE] = {
            255, 255,        // 0,1   bottom-right (383.0 mm^2/die) - reference segment
            255,             // 2     dead
            255, 255,        // 3,4   top-right (346.5 mm^2/die)
            255, 255, 255,   // 5,6,7 top (348.0 mm^2/die)
            138,             // 8     upper-left (172.5 mm^2/die)
            132,             // 9     mid-left (165.5 mm^2/die)
            149,             // 10    bottom-left (187.0 mm^2/die)
            255, 255, 255,   // 11,12,13 bottom (367.7 mm^2/die)
            132,             // 14    mid-right (165.5 mm^2/die)
            139,             // 15    center (174.5 mm^2/die)
        };

        return moduleSlot < Board::PIXELS_PER_MODULE ? FACTORS[moduleSlot] : 255;
    }

    /** Centre block (0-9) plus every digit module, by absolute chain slot. */
    static uint8_t slotScale(const uint16_t slot) {
        constexpr uint8_t BORDER_FACTOR = 189;   // 236.5 mm^2/die, same formula, no special case

        if (slot >= Board::LED_COUNT) {
            return 255;
        }

        if (slot < Board::DIGIT_BASE) {
            return (slot == 4 || slot == 9) ? 255 : BORDER_FACTOR;   // 4/9 = back indicators
        }

        return moduleFactor((slot - Board::DIGIT_BASE) % Board::PIXELS_PER_MODULE);
    }

    /** In place: every render rewrites every live slot first, so this never compounds. */
    static void compensate(CRGB *pixels) {
        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            const uint8_t factor = slotScale(slot);
            if (factor != 255) {
                pixels[slot].nscale8(factor);
            }
        }
    }
};

#endif //NINE_SEGMENT_BRIGHTNESS_H
