#ifndef DISPLAY_H
#define DISPLAY_H
#include "Glyph.h"


class GlyphDisplay {
    Adafruit_NeoPixel &pixels;
    Glyph glyphA = Glyph(pixels, GlyphId::A);
    Glyph glyphB = Glyph(pixels, GlyphId::B);
    Glyph glyphC = Glyph(pixels, GlyphId::C);
    Glyph glyphD = Glyph(pixels, GlyphId::D);

public:
    explicit GlyphDisplay(Adafruit_NeoPixel &pixels) : pixels(pixels) {
    }

    void setValue(const uint8_t valueA, const uint8_t valueB) {
        glyphA.setValue(valueA / 10);
        glyphB.setValue(valueA % 10);
        glyphC.setValue(valueB / 10);
        glyphD.setValue(valueB % 10);
    }

    void show() const {
        glyphA.show();
        glyphB.show();
        glyphC.show();
        glyphD.show();
    }
};


#endif //DISPLAY_H
