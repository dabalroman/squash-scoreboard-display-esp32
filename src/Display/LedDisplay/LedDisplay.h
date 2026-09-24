#ifndef LED_DISPLAY_H
#define LED_DISPLAY_H

#include <utility>

#include "Color.h"
#include "DisplayProfile.h"
#include "LedBar.h"
#include "LedCentralScreenBorder.h"
#include "LedGlyph.h"
#include "LedText.h"
#include "Animation/LedSweepAnimation.h"
#include "Layers/LedBlend.h"
#include "Layers/LedBreathingAnimation.h"
#include "Layers/LedLayerStack.h"
#include "Layers/LedTarget.h"

class LedDisplay {
    CRGB *pixels;
    uint32_t tickMs = 0;
    bool sameSideMode = false;

    uint8_t requestedBrightness = 255;
    uint8_t brightnessCap = 255;

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

    // Slot -> LedTarget bits, from the components' own segment tables.
    uint16_t elementMap[Board::LED_COUNT] = {};
    // Boot and celebration share the sweep; it only ever draws into a layer.
    LedSweepAnimation sweep = LedSweepAnimation(LedSweepAnimation::celebrationParams());
    LedBreathingAnimation breathing;
    LedLayerStack layers{elementMap};
    BlendMode celebrationBlend = BlendMode::Normal;   // picked on V1 among Normal/Screen/Add/Lighten, 2026-09-24

    void buildElementMap() {
        glyphA.markSlots(elementMap, LedTarget::DigitA);
        glyphB.markSlots(elementMap, LedTarget::DigitB);
        glyphC.markSlots(elementMap, LedTarget::DigitC);
        glyphD.markSlots(elementMap, LedTarget::DigitD);
        glyphColon.markSlots(elementMap, LedTarget::Colon);
        glyphIndicatorPlayerA.markSlots(elementMap, LedTarget::IndicatorA);
        glyphIndicatorPlayerB.markSlots(elementMap, LedTarget::IndicatorB);
        border.markSlots(elementMap, LedTarget::BorderTop, LedTarget::BorderBottom);
#if BOARD_REV == 1
        bar.markSlots(elementMap, LedTarget::Bar);
#endif
    }

    void stopLayers() {
        sweep.stop();
        breathing.stop();
        layers.clearAll();
    }

public:

    explicit LedDisplay(CRGB *pixels) : pixels(pixels) {
        buildElementMap();
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

    void setGlyphsGlyph(const LedWord &word) {
        setGlyphsGlyph(word.a, word.b, word.c, word.d);
    }

    // Words come from src/Strings.h as text - see LedText.h for the mapping.
    void setGlyphsText(const char *text) {
        setGlyphsGlyph(LedText::toWord(text));
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

    /** Detaches both layers; V1 also clears the bar. */
    void resetAnimations() {
        stopLayers();
        bar.setState({});
    }
#else
    template <typename MakePixels>
    void setLedBarState(MakePixels) {
        // Unevaluated operand: a call site passing a raw array still fails to
        // compile on V2 as well as V1. Nothing runs.
        (void) sizeof(decltype(std::declval<MakePixels &>()()));
    }

    void resetAnimations() {
        stopLayers();
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

    /**
     * The sweep on layer 2 over the whole front, so the result stays readable
     * under it. `winnerOnLeft` puts its origin on that player's half.
     */
    void startCelebration(const Color color, const bool winnerOnLeft) {
        sweep.setParams(LedSweepAnimation::celebrationParams());
        sweep.setSolidColor(CRGB(color.r, color.g, color.b));
        sweep.setOriginToHalf(winnerOnLeft);
        sweep.start(millis());
        layers.set(LedLayerStack::LAYER_2, &sweep, celebrationBlend, LedTarget::Front, LayerMask::AllSlots);
    }

    // Applies from the next startCelebration(); nothing in production changes it.
    void setCelebrationBlend(const BlendMode mode) {
        celebrationBlend = mode;
    }

    // Dims the lit front on layer 1. No view uses it yet; resetAnimations() stops it.
    void setBreathing(const bool enabled) {
        if (enabled) {
            breathing.start(millis());
            layers.set(LedLayerStack::LAYER_1, &breathing, BlendMode::Multiply, LedTarget::Front, LayerMask::LitOnly);
            return;
        }

        breathing.stop();
        layers.clear(LedLayerStack::LAYER_1);
    }

    /**
     * One boot-sweep frame at `elapsedMs`: the sweep screened over a black base,
     * which is exactly the sweep alone (check_boot.sh holds it byte-identical to
     * the pre-layer frames). Clock-free for the host checks.
     */
    void renderBootFrame(const uint32_t elapsedMs) {
        for (uint16_t i = 0; i < Board::LED_COUNT; i++) {
            pixels[i] = CRGB(0, 0, 0);
        }

        sweep.setParams(LedSweepAnimation::bootParams());
        sweep.setOrigin(0.0f, 0.0f);
        sweep.start(0);
        layers.clearAll();
        layers.set(LedLayerStack::LAYER_2, &sweep, BlendMode::Screen, LedTarget::Front, LayerMask::AllSlots);
        layers.compose(pixels, elapsedMs);
    }

    /** Blocking. setup() only - never from loop(). */
    void playBootSweep() {
        const uint32_t durationMs = LedSweepAnimation::bootParams().durationMs;
        const uint32_t startedMs = millis();

        for (uint32_t elapsed = 0; elapsed < durationMs; elapsed = millis() - startedMs) {
            renderBootFrame(elapsed);
            showCompensated(pixels);
            delay(LedSweepAnimation::FRAME_DELAY_MS);
        }

        resetAnimations();
        FastLED.clear();
        showCompensated(pixels);
    }

    /** LedTarget bits of one slot - for the host checks. */
    uint16_t elementsAt(const uint16_t slot) const {
        return elementMap[slot];
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
        showCompensated(pixels);
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

        // Layers blend onto the finished base; blink's dark phase is simply unlit.
        layers.compose(pixels, tickMs);
    }

    void setBrightness(const uint8_t brightness) {
        requestedBrightness = static_cast<uint8_t>(brightness * Board::GLOBAL_BRIGHTNESS_SCALE);
        applyBrightness();
    }

    void setLowPowerMode(const bool lowPowerMode) {
        brightnessCap = lowPowerMode ? 31 : 255;
        applyBrightness();
    }

private:
    void applyBrightness() const {
        FastLED.setBrightness(requestedBrightness < brightnessCap ? requestedBrightness : brightnessCap);
    }
};


#endif //LED_DISPLAY_H
