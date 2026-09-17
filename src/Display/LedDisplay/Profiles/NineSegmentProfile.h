#ifndef NINE_SEGMENT_PROFILE_H
#define NINE_SEGMENT_PROFILE_H

#include "Board.h"
#include "Display/LedDisplay/GlyphMasks.h"

/**
 * V2 - four 9-segment modules, 16 addressable slots each (21 LEDs, some wired in
 * parallel). Per-module layout, slot indices relative to the module start:
 *
 *          [5][6][7]           top
 *     [8]           [3][4]     upper-left / top-right
 *     [9]   [15]      [14]     mid-left / center / mid-right
 *    [10]           [0][1]     bottom-left / bottom-right
 *         [11][12][13]         bottom
 *
 * Slot 2 of every module is permanently dead (absolute 12, 28, 44, 60).
 *
 * The modules sit on the chain in REVERSE order (verified on the device
 * 2026-09-17): digit A (leftmost) is slots 58-73, D (rightmost) is 10-25.
 * Slots 0-9 are the centre block: border (its own class) + back indicators
 * B=4 (back-left), A=9 (back-right).
 * V2 has no colon.
 */

namespace NineSegment {
    // Indexed by mask bit.
    constexpr Segment moduleSegments[9] = {
        {3, {11, 12, 13}}, // 0 bottom
        {1, {10, 0, 0}},   // 1 bottom-left
        {2, {0, 1, 0}},    // 2 bottom-right
        {1, {15, 0, 0}},   // 3 center
        {1, {8, 0, 0}},    // 4 upper-left
        {2, {3, 4, 0}},    // 5 top-right
        {3, {5, 6, 7}},    // 6 top
        {1, {9, 0, 0}},    // 7 mid-left
        {1, {14, 0, 0}},   // 8 mid-right
    };

    constexpr Segment playerAIndicator[1] = {{1, {9, 0, 0}}};
    constexpr Segment playerBIndicator[1] = {{1, {4, 0, 0}}};

    /** digitIndex 0 = A (leftmost). Modules are wired right-to-left. */
    constexpr uint16_t moduleOffset(const uint8_t digitIndex) {
        return Board::DIGIT_BASE + (Board::MODULE_COUNT - 1 - digitIndex) * Board::PIXELS_PER_MODULE;
    }
}

struct NineSegmentProfile {
    enum : uint16_t { PIXELS_USED = Board::DIGIT_BASE + Board::MODULE_COUNT * Board::PIXELS_PER_MODULE };

    static uint16_t maskFor(const uint8_t glyph) {
        return GlyphMasks[glyph];
    }

    static SegmentTable segmentsFor(const GlyphId id) {
        using namespace NineSegment;

        switch (id) {
            default:
            case GlyphId::A:
                return {9, moduleSegments, moduleOffset(0)};
            case GlyphId::B:
                return {9, moduleSegments, moduleOffset(1)};
            case GlyphId::C:
                return {9, moduleSegments, moduleOffset(2)};
            case GlyphId::D:
                return {9, moduleSegments, moduleOffset(3)};
            case GlyphId::Colon:
                return {0, nullptr, 0};   // no colon LEDs on V2
            case GlyphId::IndicatorPlayerA:
                return {1, playerAIndicator, 0};
            case GlyphId::IndicatorPlayerB:
                return {1, playerBIndicator, 0};
        }
    }
};

#endif //NINE_SEGMENT_PROFILE_H
