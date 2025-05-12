#ifndef DISPLAY_H
#define DISPLAY_H

#include "Color.h"
#include "GlyphDisplayUnit.h"


class GlyphDisplay {
    CRGB *pixels;
    uint32_t tickMs = 0;

public:
    GlyphDisplayUnit glyphA = GlyphDisplayUnit(pixels, GlyphId::A);
    GlyphDisplayUnit glyphB = GlyphDisplayUnit(pixels, GlyphId::B);
    GlyphDisplayUnit glyphC = GlyphDisplayUnit(pixels, GlyphId::C);
    GlyphDisplayUnit glyphD = GlyphDisplayUnit(pixels, GlyphId::D);
    GlyphDisplayUnit glyphColon = GlyphDisplayUnit(pixels, GlyphId::Colon);
    GlyphDisplayUnit glyphIndicatorPlayerA = GlyphDisplayUnit(pixels, GlyphId::IndicatorPlayerA);
    GlyphDisplayUnit glyphIndicatorPlayerB = GlyphDisplayUnit(pixels, GlyphId::IndicatorPlayerB);

    explicit GlyphDisplay(CRGB *pixels) : pixels(pixels) {
        setColonAppearance();
        setPlayersIndicatorsState(false);
    }

    void initForSquashMode() {
        setColonAppearance();
        setPlayersIndicatorsState(true);
        setIndicatorAppearancePlayerA(Colors::Black);
        setIndicatorAppearancePlayerB(Colors::Black);
    }

    void initForConfigMode() {
        setColonAppearance();
        setGlyphsGlyph(Glyph::Empty, Glyph::Empty, Glyph::Empty, Glyph::Empty);
        setPlayersIndicatorsState(true);
        setIndicatorAppearancePlayerA(Colors::Black);
        setIndicatorAppearancePlayerB(Colors::Black);
    }

    void setNumericValue(const uint8_t valueA, const uint8_t valueB) {
        glyphA.setToDigit(valueA / 10);
        glyphB.setToDigit(valueA % 10);
        glyphC.setToDigit(valueB / 10);
        glyphD.setToDigit(valueB % 10);
    }

    void setGlyphsGlyph(const Glyph a, const Glyph b, const Glyph c, const Glyph d) {
        glyphA.setGlyph(a);
        glyphB.setGlyph(b);
        glyphC.setGlyph(c);
        glyphD.setGlyph(d);
    }

    void setGlyphsColor(const Color colorA, const Color colorB, const Color colorC, const Color colorD) {
        glyphA.setColor(colorA);
        glyphB.setColor(colorB);
        glyphC.setColor(colorC);
        glyphD.setColor(colorD);
    }

    void setGlyphsColor(const Color colorA, const Color colorB) {
        setGlyphsColor(colorA, colorA, colorB, colorB);
    }

    void setGlyphsAppearance(const Color colorA, const Color colorB, const bool isBlinkingA = false, const bool isBlinkingB = false) {
        setGlyphsColor(colorA, colorA, colorB, colorB);
        setGlyphBlinking(isBlinkingA, isBlinkingB);
    }

    void setGlyphBlinking(
        const bool isBlinkingA,
        const bool isBlinkingB,
        const bool isBlinkingC,
        const bool isBlinkingD
    ) {
        glyphA.setBlinking(isBlinkingA);
        glyphB.setBlinking(isBlinkingB);
        glyphC.setBlinking(isBlinkingC);
        glyphD.setBlinking(isBlinkingD);
    }

    void setGlyphBlinking(const bool isBlinkingA, const bool isBlinkingB) {
        setGlyphBlinking(isBlinkingA, isBlinkingA, isBlinkingB, isBlinkingB);
    }

    void setColonAppearance(const Color color = Colors::Black, const bool isBlinking = false) {
        glyphColon.setColor(color);
        glyphColon.setBlinking(isBlinking);
    }

    void setPlayersIndicatorsState(const bool enabled) {
        glyphIndicatorPlayerA.setGlyph(enabled ? Glyph::All : Glyph::Empty);
        glyphIndicatorPlayerB.setGlyph(enabled ? Glyph::All : Glyph::Empty);
    }

    void setIndicatorAppearancePlayerA(const Color color, const bool isBlinking = false) {
        glyphIndicatorPlayerA.setColor(color);
        glyphIndicatorPlayerA.setBlinking(isBlinking);
    }

    void setIndicatorAppearancePlayerB(const Color color, const bool isBlinking = false) {
        glyphIndicatorPlayerB.setColor(color);
        glyphIndicatorPlayerB.setBlinking(isBlinking);
    }

    static Glyph digitToGlyph(const uint8_t digit) {
        if (digit > 9) {
            return Glyph::Empty;
        }

        return static_cast<Glyph>(digit);
    }

    void display() {
        FastLED.clear();
        render();
        FastLED.show();
    }

    void render() {
        tickMs = millis();

        glyphA.render(tickMs);
        glyphB.render(tickMs);
        glyphC.render(tickMs);
        glyphD.render(tickMs);
        glyphColon.render(tickMs);
        glyphIndicatorPlayerA.render(tickMs);
        glyphIndicatorPlayerB.render(tickMs);
    }

    static void setBrightness(const uint8_t brightness) {
        FastLED.setBrightness(brightness);
    }
};


#endif //DISPLAY_H
