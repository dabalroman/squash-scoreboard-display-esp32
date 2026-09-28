#ifndef LED_DISPLAY_H
#define LED_DISPLAY_H

#include <utility>

#include "Color.h"
#include "DisplayProfile.h"
#include "LedBar.h"
#include "LedCentralScreenBorder.h"
#include "LedGlyph.h"
#include "LedText.h"
#include "Animation/LedBajgielAnimation.h"
#include "Animation/LedIntroAnimation.h"
#include "Animation/LedSweepAnimation.h"
#include "Layers/LedBlend.h"
#include "Layers/LedBreathingAnimation.h"
#include "Layers/LedSmokeAnimation.h"
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
    LedIntroAnimation intro;
    LedSmokeAnimation smoke;
    LedBajgielAnimation bajgiel;
    LedLayerStack layers{elementMap};
    BlendMode celebrationBlend = BlendMode::Normal;   // picked on V1 among Normal/Screen/Add/Lighten, 2026-09-24
    BlendMode smokeBlend = BlendMode::Screen;   // default for #59; setSmokeBlend() is the knob, picked on V1

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

#if BOARD_REV == 1
    // Every bar state goes through here, so the owner bits always match it.
    void applyBarState(std::array<LedBarPixel, LedBar::PIXEL_COUNT> state) {
        bar.setState(std::move(state));
        bar.markOwners(elementMap, LedTarget::BarLeft, LedTarget::BarRight);
    }
#endif

    void stopLayers() {
        sweep.stop();
        breathing.stop();
        intro.stop();
        smoke.stop();
        bajgiel.stop();
        layers.clearAll();
    }

    /**
     * Single LAYER_2-ownership check, shared by the smoke (#59) and the
     * comeback burst (#60) - only one of them may render there during
     * GamePlaying. `startComeback()` stops the smoke before claiming the
     * layer, so while a burst runs `smoke.active(0)` is always false; once
     * the sweep goes inactive on its own (one 800 ms cycle, no explicit
     * stop), the smoke is free to retake the layer on the next setOnFire().
     */
    bool smokeOwnsLayer2() const {
        return smoke.active(0);   // ignores nowMs, same as breathing.active(0) elsewhere
    }

    bool comebackOwnsLayer2() const {
        return sweep.active(millis());   // sweep is comeback-only during GamePlaying
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
        applyBarState(makePixels());
    }

    /** Detaches both layers; V1 also clears the bar. */
    void resetAnimations() {
        stopLayers();
        applyBarState({});
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
        smoke.stop();   // releases LAYER_2 so setOnFire(0) later can't mistake itself for the owner
        bajgiel.stop();
        sweep.setParams(LedSweepAnimation::celebrationParams());
        sweep.setSolidColor(CRGB(color.r, color.g, color.b));
        sweep.setOriginToHalf(winnerOnLeft);
        sweep.start(millis());
        layers.set(LedLayerStack::LAYER_2, &sweep, celebrationBlend, LedTarget::Front, LayerMask::AllSlots);
    }

    /**
     * A game won to zero: the loser's two 0s spin as a comet in their own colour,
     * instead of the sweep. Multiply + LitOnly, so it only dims the lit ring and
     * the frame after it expires is the steady score.
     */
    void startBajgiel(const bool loserOnLeft) {
        smoke.stop();
        sweep.stop();
        bajgiel.start(
            millis(), elementMap,
            loserOnLeft ? LedTarget::DigitA : LedTarget::DigitC,
            loserOnLeft ? LedTarget::DigitB : LedTarget::DigitD
        );
        layers.set(
            LedLayerStack::LAYER_2, &bajgiel, BlendMode::Multiply,
            loserOnLeft ? LedTarget::LeftScore : LedTarget::RightScore, LayerMask::LitOnly
        );
    }

    bool celebrationActive() const {
        const uint32_t now = millis();
        return sweep.active(now) || bajgiel.active(now);
    }

    // Layer 2 only: unlike resetAnimations(), V1's history bar survives.
    void stopCelebration() {
        sweep.stop();
        bajgiel.stop();
        layers.clear(LedLayerStack::LAYER_2);
    }

    // Applies from the next startCelebration(); nothing in production changes it.
    void setCelebrationBlend(const BlendMode mode) {
        celebrationBlend = mode;
    }

    /**
     * One-cycle burst on LAYER_2 for a point that breaks the opponent's on-fire streak (#60).
     * Reuses the same `sweep` member as celebration - the two never coexist,
     * celebration is GameCelebration-only, this is GamePlaying-only. Stops the
     * smoke first so it releases LAYER_2 (comebackOwnsLayer2() then holds until
     * the single cycle ends); a new burst restarts an active one.
     */
    void startComeback(const Color color, const bool onLeft) {
        smoke.stop();
        sweep.setParams(LedSweepAnimation::comebackParams());
        sweep.setSolidColor(CRGB(color.r, color.g, color.b));
        sweep.setOriginToHalf(onLeft);
        sweep.start(millis());
        layers.set(LedLayerStack::LAYER_2, &sweep, BlendMode::Normal, LedTarget::Front, LayerMask::AllSlots);
    }

    /**
     * The walk-on wipe on layer 2, left half `left`, right half `right`. Bar
     * slots erase after the hold (empty at GamePlaying's 0:0); border top/bottom
     * take left/right regardless of x-half (its segments straddle the seam).
     */
    void startIntro(const Color left, const Color right) {
        smoke.stop();   // releases LAYER_2 so setOnFire(0) later can't mistake itself for the owner
        intro.start(
            millis(), CRGB(left.r, left.g, left.b), CRGB(right.r, right.g, right.b),
            elementMap, LedTarget::Bar, LedTarget::BorderTop, LedTarget::BorderBottom
        );
        layers.set(LedLayerStack::LAYER_2, &intro, BlendMode::Normal, LedTarget::Front, LayerMask::AllSlots);
    }

    bool introActive() const {
        return intro.active(millis());
    }

    /**
     * Dims the lit `targets` on layer 1; 0 stops it. Called every frame, so the
     * phase restarts only when it was off - a repeat call just moves the mask.
     * resetAnimations() stops it.
     */
    void setBreathing(const uint16_t targets) {
        if (targets == 0) {
            breathing.stop();
            layers.clear(LedLayerStack::LAYER_1);
            return;
        }

        if (!breathing.active(0)) breathing.start(millis());
        layers.set(LedLayerStack::LAYER_1, &breathing, BlendMode::Multiply, targets, LayerMask::LitOnly);
    }

    /** Game ball: a side's digits, border half and the bar pixels it scored. */
    static uint16_t breathingTargets(const bool left, const bool right) {
        uint16_t targets = 0;
        if (left) targets |= LedTarget::LeftScore | LedTarget::BorderTop | LedTarget::BarLeft;
        if (right) targets |= LedTarget::RightScore | LedTarget::BorderBottom | LedTarget::BarRight;
        return targets;
    }

    /**
     * "On fire" (task #59): rising smoke over the lit `targets` on LAYER_2.
     * 0 stops it only when the smoke currently owns LAYER_2 - never clobbers
     * a celebration or intro running there. Called every frame, so the drift
     * restarts only on off -> on; a repeat call just moves the mask.
     *
     * Yields to a running comeback burst (#60) either way: while one owns
     * LAYER_2 this neither starts/claims the smoke (non-zero) nor clears the
     * burst (0). Views call this before taking the comeback side each frame,
     * so a burst started this same frame is not yet active here and still
     * wins the layer once its own startComeback() runs afterwards.
     */
    void setOnFire(const uint16_t targets) {
        if (comebackOwnsLayer2()) {
            return;
        }

        if (targets == 0) {
            if (smokeOwnsLayer2()) {
                smoke.stop();
                layers.clear(LedLayerStack::LAYER_2);
            }
            return;
        }

        if (!smoke.active(0)) smoke.start(millis());
        layers.set(LedLayerStack::LAYER_2, &smoke, smokeBlend, targets, LayerMask::LitOnly);
    }

    /** On fire covers the side's digits and its own V1 bar pixels - not the border. */
    static uint16_t onFireTargets(const bool left, const bool right) {
        uint16_t targets = 0;
        if (left) targets |= LedTarget::LeftScore | LedTarget::BarLeft;
        if (right) targets |= LedTarget::RightScore | LedTarget::BarRight;
        return targets;
    }

    // Applies from the next setOnFire(); nothing in production calls it - the user picks on V1.
    void setSmokeBlend(const BlendMode mode) {
        smokeBlend = mode;
    }

    /**
     * One boot-sweep frame at `elapsedMs`: the sweep screened over a black base,
     * which is exactly the sweep alone (test_boot holds it byte-identical to
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
