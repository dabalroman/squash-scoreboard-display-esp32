#ifndef SEVEN_SEGMENT_PROFILE_H
#define SEVEN_SEGMENT_PROFILE_H

#include "Display/LedDisplay/GlyphMasks.h"

/**
 * V1 - 112-LED 7-segment scoreboard. Every segment is 3 LEDs; the strip zig-zags,
 * so the digit tables are hand-written (absolute pixel indices, base 0).
 *
 * LEDs and segment ids (bits)
 *
 *                0x40
 *             71, 72, 73
 *          53            63
 *    0x10  52            62  0x20
 *          51            61
 *             41, 42, 43
 *          23    0x8     33
 *     0x2  22            32  0x4
 *          21            31
 *             11, 12, 13
 *                0x1
 */

namespace SevenSegment {
    constexpr Segment glyphA[7] = {
        {3, {87, 86, 85}},
        {3, {51, 50, 49}},
        {3, {52, 53, 54}},
        {3, {76, 77, 78}},
        {3, {48, 47, 46}},
        {3, {55, 56, 57}},
        {3, {75, 74, 73}},
    };

    constexpr Segment glyphB[7] = {
        {3, {84, 83, 82}},
        {3, {63, 62, 61}},
        {3, {64, 65, 66}},
        {3, {79, 80, 81}},
        {3, {60, 59, 58}},
        {3, {67, 68, 69}},
        {3, {72, 71, 70}},
    };

    constexpr Segment glyphC[7] = {
        {3, {45, 44, 43}},
        {3, {9, 8, 7}},
        {3, {10, 11, 12}},
        {3, {34, 35, 36}},
        {3, {6, 5, 4}},
        {3, {13, 14, 15}},
        {3, {33, 32, 31}},
    };

    constexpr Segment glyphD[7] = {
        {3, {42, 41, 40}},
        {3, {21, 20, 19}},
        {3, {22, 23, 24}},
        {3, {37, 38, 39}},
        {3, {18, 17, 16}},
        {3, {25, 26, 27}},
        {3, {30, 29, 28}},
    };

    constexpr Segment glyphColon[1] = {{3, {0, 0, 1}}};
    constexpr Segment glyphPlayerAIndicator[1] = {{3, {3, 3, 3}}};
    constexpr Segment glyphPlayerBIndicator[1] = {{3, {2, 2, 2}}};
}

struct SevenSegmentProfile {
    enum : uint16_t { PIXELS_USED = 112 };

    /** 7-segment collapse: keep CENTER as the middle bar, drop the mid-row fill. */
    static uint16_t maskFor(const uint8_t glyph) {
        return GlyphMasks[glyph] & 0x7F;
    }

    static SegmentTable segmentsFor(const GlyphId id) {
        using namespace SevenSegment;

        switch (id) {
            default:
            case GlyphId::A:
                return {7, glyphA, 0};
            case GlyphId::B:
                return {7, glyphB, 0};
            case GlyphId::C:
                return {7, glyphC, 0};
            case GlyphId::D:
                return {7, glyphD, 0};
            case GlyphId::Colon:
                return {1, glyphColon, 0};
            case GlyphId::IndicatorPlayerA:
                return {1, glyphPlayerAIndicator, 0};
            case GlyphId::IndicatorPlayerB:
                return {1, glyphPlayerBIndicator, 0};
        }
    }
};

#endif //SEVEN_SEGMENT_PROFILE_H
