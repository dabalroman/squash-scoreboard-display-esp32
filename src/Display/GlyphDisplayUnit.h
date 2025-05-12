#ifndef DISPLAYDIGIT_H
#define DISPLAYDIGIT_H

#include <Arduino.h>
#include <FastLED.h>

#include "Color.h"

#define GLYPH_DISPLAY_UNIT_BLINK_INTERVAL_MS 1000

/**
 * LEDs and segment ids (masks)
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

constexpr uint8_t SegmentToGlyphMap[17] = {
    0b01110111, // 0
    0b00100100, // 1
    0b01101011, // 2
    0b01101101, // 3
    0b00111100, // 4
    0b01011101, // 5
    0b01011111, // 6
    0b01100100, // 7
    0b01111111, // 8
    0b01111101, // 9
    0b01111110, // A
    0b01111010, // P
    0b00001000, // Minus
    0b00001111, // LowerDot
    0b01111000, // UpperDot
    0b11111111, // All
    0b00000000, // Empty
};

enum class Glyph: uint8_t {
    D0 = 0,
    D1 = 1,
    D2 = 2,
    D3 = 3,
    D4 = 4,
    D5 = 5,
    D6 = 6,
    D7 = 7,
    D8 = 8,
    D9 = 9,
    A = 10,
    P = 11,
    Minus = 12,
    LowerDot = 13,
    UpperDot = 14,
    All = 15,
    Empty = 16
};

enum class GlyphId: uint8_t {
    A = 0,
    B = 1,
    C = 2,
    D = 3,
    Colon = 4,
    IndicatorPlayerA = 5,
    IndicatorPlayerB = 6
};

struct PixelsToSegmentMap {
    uint8_t segments[7][3];
};

constexpr PixelsToSegmentMap glyphA = {
    {
        {87, 86, 85},
        {51, 50, 49},
        {52, 53, 54},
        {76, 77, 78},
        {48, 47, 46},
        {55, 56, 57},
        {75, 74, 73}
    },
};

constexpr PixelsToSegmentMap glyphB = {
    {
        {84, 83, 82},
        {63, 62, 61},
        {64, 65, 66},
        {79, 80, 81},
        {60, 59, 58},
        {67, 68, 69},
        {72, 71, 70}
    },
};

constexpr PixelsToSegmentMap glyphC = {
    {
        {45, 44, 43},
        {9, 8, 7},
        {10, 11, 12},
        {34, 35, 36},
        {6, 5, 4},
        {13, 14, 15},
        {33, 32, 31}
    },
};

constexpr PixelsToSegmentMap glyphD = {
    {
        {42, 41, 40},
        {21, 20, 19},
        {22, 23, 24},
        {37, 38, 39},
        {18, 17, 16},
        {25, 26, 27},
        {30, 29, 28}
    },
};

constexpr PixelsToSegmentMap glyphColon = {
    {{0, 0, 1}}
};

constexpr PixelsToSegmentMap glyphPlayerAIndicator = {
    {{3, 3, 3}}
};

constexpr PixelsToSegmentMap glyphPlayerBIndicator = {
    {{2, 2, 2}}
};

class GlyphDisplayUnit {
protected:
    GlyphId glyphId;
    CRGB *pixels;

    uint8_t value = 0;
    CRGB color = CRGB::White;
    bool isBlinking = false;

public:
    GlyphDisplayUnit(CRGB *pixels, const GlyphId glyphId) : glyphId(glyphId), pixels(pixels) {
    }

    static const PixelsToSegmentMap *getGlyphPixels(const GlyphId glyph) {
        switch (glyph) {
            default:
            case GlyphId::A:
                return &glyphA;
            case GlyphId::B:
                return &glyphB;
            case GlyphId::C:
                return &glyphC;
            case GlyphId::D:
                return &glyphD;
            case GlyphId::Colon:
                return &glyphColon;
            case GlyphId::IndicatorPlayerA:
                return &glyphPlayerAIndicator;
            case GlyphId::IndicatorPlayerB:
                return &glyphPlayerBIndicator;
        }
    }

    Glyph getGlyph() const {
        return static_cast<Glyph>(value);
    }

    void setGlyph(Glyph glyph) {
        this->value = static_cast<uint8_t>(glyph);
    }

    void setToDigit(uint8_t digit) {
        if (digit > 9) {
            digit = 0;
        }

        this->value = digit;
    }

    void setColor(const CRGB color) {
        this->color = color;
    }

    void setColor(const Color color) {
        this->color = CRGB(color.r, color.g, color.b);
    }

    void setBlinking(const bool isBlinking) {
        this->isBlinking = isBlinking;
    }

    void render(const uint32_t &tickMs) const {
        if (isBlinking && tickMs % GLYPH_DISPLAY_UNIT_BLINK_INTERVAL_MS < GLYPH_DISPLAY_UNIT_BLINK_INTERVAL_MS / 4) {
            return;
        }

        const uint8_t amountOfSegments =
                glyphId == GlyphId::Colon
                || glyphId == GlyphId::IndicatorPlayerA
                || glyphId == GlyphId::IndicatorPlayerB
                    ? 1
                    : 7;

        const auto *glyphSegments = getGlyphPixels(glyphId)->segments;
        for (uint8_t segment = 0; segment < amountOfSegments; segment++) {
            if (SegmentToGlyphMap[value] >> segment & 0x1) {
                for (uint8_t i = 0; i < 3; i++) {
                    pixels[glyphSegments[segment][i]] = color;
                }
            }
        }
    }
};


#endif //DISPLAYDIGIT_H
