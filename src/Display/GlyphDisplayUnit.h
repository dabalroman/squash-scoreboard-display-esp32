#ifndef DISPLAYDIGIT_H
#define DISPLAYDIGIT_H

#include <Arduino.h>
#include <FastLED.h>

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

constexpr uint8_t SegmentToGlyphMap[14] = {
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
    0b00000001, // Colon
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
    Colon = 12,
    Empty = 13
};

enum class GlyphId: uint8_t {
    A = 0,
    B = 1,
    C = 2,
    D = 3,
    Colon = 4,
};

struct PixelsToSegmentMap {
    uint8_t segments[7][3];
};

constexpr PixelsToSegmentMap glyphA = {
    {
        {85, 84, 83},
        {49, 48, 47},
        {50, 51, 52},
        {74, 75, 76},
        {46, 45, 44},
        {53, 54, 55},
        {73, 72, 71}
    },
};

constexpr PixelsToSegmentMap glyphB = {
    {
        {82, 81, 80},
        {61, 60, 59},
        {62, 63, 64},
        {77, 78, 79},
        {58, 57, 56},
        {65, 66, 67},
        {70, 69, 68}
    },
};


constexpr PixelsToSegmentMap glyphC = {
    {
        {43, 42, 41},
        {7, 6, 5},
        {8, 9, 10},
        {32, 33, 34},
        {4, 3, 2},
        {11, 12, 13},
        {31, 30, 29}
    },
};

constexpr PixelsToSegmentMap glyphD = {
    {
        {40, 39, 38},
        {19, 18, 17},
        {20, 21, 22},
        {35, 36, 37},
        {16, 15, 14},
        {23, 24, 25},
        {28, 27, 26}
    },
};

constexpr PixelsToSegmentMap glyphColon = {
    {{0, 0, 1}}
};

class GlyphDisplayUnit {
    GlyphId glyphId;
    uint8_t value = 0;
    CRGB *pixels;

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
        }
    }

    Glyph getGlyph() {
        return static_cast<Glyph>(value);
    }

    void setGlyph(Glyph glyph) {
        this->value = static_cast<uint8_t>(glyph);
    }

    void setValue(uint8_t value) {
        if (value > 9) {
            value = 0;
        }

        this->value = value;
    }

    void show() const {
        const uint8_t amountOfSegments = glyphId == GlyphId::Colon ? 1 : 7;

        const auto *glyphSegments = getGlyphPixels(glyphId)->segments;
        for (uint8_t segment = 0; segment < amountOfSegments; segment++) {
            if (SegmentToGlyphMap[value] >> segment & 0x1) {
                for (uint8_t i = 0; i < 3; i++) {
                    pixels[glyphSegments[segment][i]] = CRGB::Aqua;
                }
            }
        }
    }
};


#endif //DISPLAYDIGIT_H
