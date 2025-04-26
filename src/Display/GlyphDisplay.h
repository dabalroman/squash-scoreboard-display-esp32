#ifndef DISPLAY_H
#define DISPLAY_H

#include "Color.h"
#include "GlyphDisplayUnit.h"


class GlyphDisplay {
    CRGB *pixels;
    GlyphDisplayUnit glyphA = GlyphDisplayUnit(pixels, GlyphId::A);
    GlyphDisplayUnit glyphB = GlyphDisplayUnit(pixels, GlyphId::B);
    GlyphDisplayUnit glyphC = GlyphDisplayUnit(pixels, GlyphId::C);
    GlyphDisplayUnit glyphD = GlyphDisplayUnit(pixels, GlyphId::D);
    GlyphDisplayUnit glyphColon = GlyphDisplayUnit(pixels, GlyphId::Colon);

public:
    explicit GlyphDisplay(CRGB *pixels) : pixels(pixels) {
    }

    void setValue(const uint8_t valueA, const uint8_t valueB) {
        glyphA.setValue(valueA / 10);
        glyphB.setValue(valueA % 10);
        glyphC.setValue(valueB / 10);
        glyphD.setValue(valueB % 10);
    }

    void setGlyphs(const Glyph a, const Glyph b, const Glyph c, const Glyph d) {
        glyphA.setGlyph(a);
        glyphB.setGlyph(b);
        glyphC.setGlyph(c);
        glyphD.setGlyph(d);
    }

    void setGlyphColor(const Color colorA, const Color colorB, const Color colorC, const Color colorD) {
        glyphA.setColor(colorA);
        glyphB.setColor(colorB);
        glyphC.setColor(colorC);
        glyphD.setColor(colorD);
    }

    void toggleColon() {
        if (glyphColon.getGlyph() == Glyph::Colon) {
            glyphColon.setGlyph(Glyph::Empty);
        } else {
            glyphColon.setGlyph(Glyph::Colon);
        }
    }

    static Glyph digitToGlyph(const uint8_t digit) {
        if (digit > 9) {
            return Glyph::Empty;
        }

        return static_cast<Glyph>(digit);
    }

    static void clear() {
        FastLED.clear();
    }

    static void show() {
        FastLED.show();
    }

    void render() {
        glyphA.render();
        glyphB.render();
        glyphC.render();
        glyphD.render();
        glyphColon.render();
    }
};


#endif //DISPLAY_H
