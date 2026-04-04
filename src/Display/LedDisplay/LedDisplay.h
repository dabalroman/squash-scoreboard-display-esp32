#ifndef LED_DISPLAY_H
#define LED_DISPLAY_H

#include "Color.h"
#include "LedBar.h"
#include "LedGlyph.h"

class LedDisplay {
    CRGB *pixels;
    uint32_t tickMs = 0;

public:
    LedGlyph glyphA = LedGlyph(pixels, GlyphId::A);
    LedGlyph glyphB = LedGlyph(pixels, GlyphId::B);
    LedGlyph glyphC = LedGlyph(pixels, GlyphId::C);
    LedGlyph glyphD = LedGlyph(pixels, GlyphId::D);
    LedGlyph glyphColon = LedGlyph(pixels, GlyphId::Colon);
    LedGlyph glyphIndicatorPlayerA = LedGlyph(pixels, GlyphId::IndicatorPlayerA);
    LedGlyph glyphIndicatorPlayerB = LedGlyph(pixels, GlyphId::IndicatorPlayerB);
    LedBar bar = LedBar(pixels);

    explicit LedDisplay(CRGB *pixels) : pixels(pixels) {
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

    void setHistoryBarState(const std::array<LedBarPixel, BAR_DISPLAY_AMOUNT_OF_PIXELS> &state) {
        bar.setState(state);
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
        bar.render(tickMs);
    }

    static void setBrightness(const uint8_t brightness) {
        FastLED.setBrightness(brightness);
    }
};


#endif //LED_DISPLAY_H
