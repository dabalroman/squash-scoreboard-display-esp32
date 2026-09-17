// Host shim for FastLED 3.9.16: CRGB plus the exact lib8tion maths the display
// layer uses (sin8_C, nscale8x3 with FASTLED_SCALE8_FIXED=1), copied verbatim.
#ifndef LED_DUMP_SHIM_FASTLED_H
#define LED_DUMP_SHIM_FASTLED_H

#include "Arduino.h"

typedef uint8_t fract8;

inline uint8_t sin8(uint8_t theta) {
    static const uint8_t b_m16_interleave[] = {0, 49, 49, 41, 90, 27, 117, 10};
    uint8_t offset = theta;
    if (theta & 0x40) {
        offset = (uint8_t)255 - offset;
    }
    offset &= 0x3F;
    uint8_t secoffset = offset & 0x0F;
    if (theta & 0x40)
        ++secoffset;
    uint8_t section = offset >> 4;
    uint8_t s2 = section * 2;
    const uint8_t *p = b_m16_interleave;
    p += s2;
    uint8_t b = *p;
    ++p;
    uint8_t m16 = *p;
    uint8_t mx = (m16 * secoffset) >> 4;
    int8_t y = mx + b;
    if (theta & 0x80)
        y = -y;
    y += 128;
    return y;
}

inline void nscale8x3(uint8_t &r, uint8_t &g, uint8_t &b, fract8 scale) {
    uint16_t scale_fixed = scale + 1;
    r = (((uint16_t)r) * scale_fixed) >> 8;
    g = (((uint16_t)g) * scale_fixed) >> 8;
    b = (((uint16_t)b) * scale_fixed) >> 8;
}

struct CRGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    CRGB() : r(0), g(0), b(0) {}
    CRGB(uint8_t ir, uint8_t ig, uint8_t ib) : r(ir), g(ig), b(ib) {}
    CRGB(uint32_t colorcode)
        : r((colorcode >> 16) & 0xFF), g((colorcode >> 8) & 0xFF), b(colorcode & 0xFF) {}

    CRGB scale8(uint8_t scaledown) const {
        CRGB out = *this;
        nscale8x3(out.r, out.g, out.b, scaledown);
        return out;
    }

    bool operator==(const CRGB &o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const CRGB &o) const { return !(*this == o); }

    // Add named colours here as headers start using them (values from FastLED crgb.h).
    enum HTMLColorCode {
        Black = 0x000000,
        RoyalBlue = 0x4169E1,
        White = 0xFFFFFF,
    };
};

class CFastLED {
    CRGB *buf = nullptr;
    int count = 0;
    uint8_t brightness = 255;

public:
    void registerBuffer(CRGB *pixels, int n) { buf = pixels; count = n; }
    void clear() { for (int i = 0; i < count; i++) buf[i] = CRGB(0, 0, 0); }
    void show() {}
    void setBrightness(uint8_t b) { brightness = b; }
    uint8_t getBrightness() const { return brightness; }
};

extern CFastLED FastLED;

#endif
