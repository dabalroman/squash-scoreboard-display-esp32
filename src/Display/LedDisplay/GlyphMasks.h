#ifndef GLYPH_MASKS_H
#define GLYPH_MASKS_H

#include <Arduino.h>

/**
 * The single glyph table shared by both displays (V1 7-segment, V2 9-segment).
 *
 *                MR ML TOP TR UL CEN BR BL BOT
 * bit             8  7   6  5  4   3  2  1   0
 *
 * Contract - do not break:
 * - Glyph indices 0..36 are frozen (same in the 9-segment rig). Append only.
 * - Bits 0..6 keep their 7-segment meaning. CENTER (bit 3) is set iff the
 *   7-segment middle bar is lit; MID_LEFT/MID_RIGHT are decorative fill only.
 *   That is what makes `mask & 0x7F` the correct 7-segment collapse. Never OR
 *   or AND the mid row instead (OR renders 0 as 8; AND drops Dot).
 * - Preview/verify after any edit: python helpers/preview_glyphs.py
 */

enum class Glyph : uint8_t {
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
    C = 11,
    F = 12,
    G = 13,
    L = 14,
    P = 15,
    U = 16,
    Y = 17,
    Minus = 18,
    LowerDot = 19,
    UpperDot = 20,
    All = 21,
    Empty = 22,
    b = 23,
    E = 24,
    H = 25,
    n = 26,
    o = 27,
    r = 28,
    t = 29,
    u = 30,
    c = 31,
    h = 32,
    S = 33,
    Z = 34,
    I = 35,
    d = 36,

    // Extended set - needs partial middle bars; degrades acceptably on 7-segment.
    Dot = 37,
    i = 38,
    J = 39,
    l = 40,
    Equals = 41,
    Underscore = 42,
    Overline = 43,
    RBracket = 44,
    Apostrophe = 45,

    COUNT
};

constexpr uint8_t GLYPH_COUNT = static_cast<uint8_t>(Glyph::COUNT);

constexpr uint16_t GlyphMasks[GLYPH_COUNT] = {
    0b111110111, // 0  - both verticals filled through the middle row
    0b100100100, // 1  - unbroken right stroke
    0b111101011, // 2
    0b111101101, // 3
    0b110111100, // 4
    0b111011101, // 5
    0b111011111, // 6
    0b101100100, // 7  - unbroken right stroke
    0b111111111, // 8
    0b111111101, // 9
    0b111111110, // A
    0b011010011, // C  - left stroke filled, no middle bar
    0b111011010, // F
    0b011010111, // G
    0b010010011, // L  - left stroke filled
    0b111111010, // P
    0b110110111, // U  - both verticals filled
    0b110111101, // Y
    0b110001000, // Minus     - full middle bar
    0b110001111, // LowerDot  - lower box
    0b111111000, // UpperDot  - upper box
    0b111111111, // All
    0b000000000, // Empty
    0b110011111, // b
    0b111011011, // E
    0b110111110, // H
    0b110001110, // n
    0b110001111, // o  - same shape as LowerDot
    0b110001010, // r
    0b110011011, // t
    0b000000111, // u  - sits below the middle row, no fill needed
    0b110001011, // c
    0b110011110, // h
    0b111011101, // S  - same shape as 5
    0b111101011, // Z  - same shape as 2
    0b100100100, // I  - same shape as 1
    0b110101111, // d

    0b000001000, // Dot        - CEN alone
    0b100000100, // i          - MR|BR, short lower-right stem
    0b100100111, // J
    0b010010010, // l          - UL|ML|BL, full left stem
    0b110001001, // Equals     - middle bar + bottom
    0b000000001, // Underscore - BOT
    0b001000000, // Overline   - TOP
    0b101100101, // RBracket   - mirror of C
    0b000100000, // Apostrophe - TR
};

/** A segment lights 1..3 slots; `pixels` are relative to its table's `base`. */
struct Segment {
    uint8_t count;
    uint8_t pixels[3];
};

/**
 * Pixel layout of one glyph position. `count` is the number of segments this
 * position really has (0 for V2's absent colon, 1 for indicators) and is the
 * ONLY valid loop bound - never a profile's widest segment count.
 */
struct SegmentTable {
    uint8_t count;
    const Segment *segments;
    uint16_t base;
};

/**
 * The four digit glyphs as one value, so a whole LED word can live in the string
 * table (src/Strings.h) next to the text that has to match it. Words with a
 * varying position copy the constant and overwrite that field.
 */
struct LedWord {
    Glyph a;
    Glyph b;
    Glyph c;
    Glyph d;
};

enum class GlyphId : uint8_t {
    A = 0,
    B = 1,
    C = 2,
    D = 3,
    Colon = 4,
    IndicatorPlayerA = 5,
    IndicatorPlayerB = 6
};

#endif //GLYPH_MASKS_H
