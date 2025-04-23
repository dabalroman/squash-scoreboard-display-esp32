#ifndef DISPLAY_H
#define DISPLAY_H
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

    void toggleColon() {
        if (glyphColon.getGlyph() == Glyph::Colon) {
            glyphColon.setGlyph(Glyph::Empty);
        } else {
            glyphColon.setGlyph(Glyph::Colon);
        }
    }

    void show() const {
        glyphA.show();
        glyphB.show();
        glyphC.show();
        glyphD.show();
        glyphColon.show();
    }
};


#endif //DISPLAY_H
