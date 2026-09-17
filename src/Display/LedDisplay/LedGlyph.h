#ifndef LED_GLYPH
#define LED_GLYPH

#include <Arduino.h>
#include <FastLED.h>

#include "Color.h"
#include "GlyphMasks.h"
#include "DisplayProfile.h"

/**
 * One glyph position (digit, colon or indicator), drawn through a compile-time
 * layout profile. Views never see segments or pixels - only Glyph, Color, blink.
 */
template <typename Profile>
class LedGlyphT {
    enum : uint16_t { BLINK_INTERVAL_MS = 500 };

protected:
    GlyphId glyphId;
    CRGB *pixels;

    uint8_t value = 0;
    CRGB color = CRGB::White;
    bool isBlinking = false;

public:
    LedGlyphT(CRGB *pixels, const GlyphId glyphId) : glyphId(glyphId), pixels(pixels) {
    }

    Glyph getGlyph() const {
        return static_cast<Glyph>(value);
    }

    void setGlyph(const Glyph glyph) {
        const auto index = static_cast<uint8_t>(glyph);
        this->value = index < GLYPH_COUNT ? index : static_cast<uint8_t>(Glyph::Empty);
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
        if (isBlinking && tickMs % BLINK_INTERVAL_MS < BLINK_INTERVAL_MS / 2) {
            return;
        }

        const SegmentTable table = Profile::segmentsFor(glyphId);
        const uint16_t mask = Profile::maskFor(value);

        // Bound by the table's own count: V2's colon has 0 segments and each
        // indicator 1, while Glyph::All sets all nine mask bits.
        for (uint8_t segment = 0; segment < table.count; segment++) {
            if (mask >> segment & 0x1) {
                const Segment &pixelsOfSegment = table.segments[segment];
                for (uint8_t i = 0; i < pixelsOfSegment.count; i++) {
                    pixels[table.base + pixelsOfSegment.pixels[i]] = color;
                }
            }
        }
    }
};

using LedGlyph = LedGlyphT<ActiveGlyphProfile>;

#endif //LED_GLYPH
