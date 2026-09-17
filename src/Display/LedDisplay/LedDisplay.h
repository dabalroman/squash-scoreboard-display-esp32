#ifndef LED_DISPLAY_H
#define LED_DISPLAY_H

#include <utility>

#include "Board.h"
#include "Color.h"
#include "LedBar.h"
#include "LedCentralScreenBorder.h"
#include "LedGlyph.h"

class LedDisplay {
    CRGB *pixels;
    uint32_t tickMs = 0;
    bool sameSideMode = false;

    LedGlyph glyphA = LedGlyph(pixels, GlyphId::A);
    LedGlyph glyphB = LedGlyph(pixels, GlyphId::B);
    LedGlyph glyphC = LedGlyph(pixels, GlyphId::C);
    LedGlyph glyphD = LedGlyph(pixels, GlyphId::D);
    LedGlyph glyphColon = LedGlyph(pixels, GlyphId::Colon);
    LedGlyph glyphIndicatorPlayerA = LedGlyph(pixels, GlyphId::IndicatorPlayerA);
    LedGlyph glyphIndicatorPlayerB = LedGlyph(pixels, GlyphId::IndicatorPlayerB);
    LedCentralScreenBorder border = LedCentralScreenBorder(pixels);

#if BOARD_REV == 1
    LedBar bar = LedBar(pixels);
#endif
    // V2 has no history bar. LedBar, LedBarPixel and the renderers stay compiled
    // on both boards (PIXEL_COUNT stays 24) so call-site lambdas still type-check.

public:

    explicit LedDisplay(CRGB *pixels) : pixels(pixels) {
        setColonAppearance();
        setPlayersIndicatorsState(false);
        setBorderEnabled(false);
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

    /**
     * Takes a lambda producing the bar pixels, never the pixels themselves: an
     * argument is evaluated even into an empty setter, and V2 must not run the
     * bar renderers at all.
     */
#if BOARD_REV == 1
    template <typename MakePixels>
    void setLedBarState(MakePixels makePixels) {
        bar.setState(makePixels());
    }

    void resetHistoryBar() {
        bar.setMode(LedBarMode::state);
        bar.setState({});
    }
#else
    template <typename MakePixels>
    void setLedBarState(MakePixels) {
        // Unevaluated operand: a call site passing a raw array still fails to
        // compile on V2 as well as V1. Nothing runs.
        (void) sizeof(decltype(std::declval<MakePixels &>()()));
    }

    void resetHistoryBar() {
    }
#endif

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

    /** V2 has no colon LEDs: its GlyphId::Colon table is empty, so this draws nothing there. */
    void setColonAppearance(const Color color = Colors::Black, const bool isBlinking = false) {
        glyphColon.setColor(color);
        glyphColon.setBlinking(isBlinking);
    }

    void setSameSideMode(const bool sameSide) {
        sameSideMode = sameSide;
    }

    void setPlayersIndicatorsState(const bool enabled) {
        glyphIndicatorPlayerA.setGlyph(enabled ? Glyph::All : Glyph::Empty);
        glyphIndicatorPlayerB.setGlyph(enabled ? Glyph::All : Glyph::Empty);
    }

    // Back-facing indicators; they follow sameSideMode. They never touch the border.
    void setIndicatorAppearancePlayerA(const Color color, const bool isBlinking = false) {
        LedGlyph &target = sameSideMode ? glyphIndicatorPlayerB : glyphIndicatorPlayerA;
        target.setColor(color);
        target.setBlinking(isBlinking);
    }

    void setIndicatorAppearancePlayerB(const Color color, const bool isBlinking = false) {
        LedGlyph &target = sameSideMode ? glyphIndicatorPlayerA : glyphIndicatorPlayerB;
        target.setColor(color);
        target.setBlinking(isBlinking);
    }

    // Front-facing legend for the e-paper rows: top = left court player, bottom = right.
    // Independent of the back indicators and never sameSideMode-redirected. V1: no-op.
    void setBorderEnabled(const bool enabled) {
        border.setEnabled(enabled);
    }

    void setBorderAppearance(
        const Color top,
        const Color bottom,
        const bool isBlinkingTop = false,
        const bool isBlinkingBottom = false
    ) {
        border.setTop(top, isBlinkingTop);
        border.setBottom(bottom, isBlinkingBottom);
    }

#if BOARD_REV == 1
    void startCelebration(const Color color) {
        bar.setCelebrationColor(CRGB(color.r, color.g, color.b));
        bar.setMode(LedBarMode::celebration);
    }
#else
    void startCelebration(const Color) {
        // Undecided on V2 (no history bar) - deliberately a no-op.
    }
#endif

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
        border.render(tickMs);
#if BOARD_REV == 1
        bar.render(tickMs);
#endif
    }

    /**
     * The brightness a view or the config asks for. What actually reaches FastLED
     * is `min(requested, cap)`, so a caller never has to know about the cap.
     */
    void setBrightness(const uint8_t brightness) {
        requestedBrightness = brightness;
        applyBrightness();
    }

    /**
     * Upper limit applied on top of the requested brightness, e.g. while the pack
     * is low. Never persisted: clearing it restores the requested value.
     */
    void setBrightnessCap(const uint8_t cap) {
        brightnessCap = cap;
        applyBrightness();
    }

private:
    void applyBrightness() const {
        FastLED.setBrightness(requestedBrightness < brightnessCap ? requestedBrightness : brightnessCap);
    }

    uint8_t requestedBrightness = 255;
    uint8_t brightnessCap = 255;
};


#endif //LED_DISPLAY_H
