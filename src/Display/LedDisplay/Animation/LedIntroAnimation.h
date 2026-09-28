#ifndef LED_INTRO_ANIMATION_H
#define LED_INTRO_ANIMATION_H

#include <Arduino.h>
#include <FastLED.h>
#include <math.h>

#include "Board.h"
#include "Display/LedDisplay/Layers/LedAnimation.h"
#include "LedSlotPositions.h"

/**
 * The walk-on before every game: a fill that wipes inward from the panel's
 * outer edge to its centre and leaves colour behind - the left half in the left
 * player's colour, the right half in the right's, then holds the full front.
 * The seam (V1's colon, x == 0) stays dark.
 *
 * A fill, not a ring: every slot the front has passed stays lit, so the sweep's
 * band/radial-gap rule does not apply. SKIP slots are never written.
 *
 * After the hold, an out phase erases the "outgoing" slots only (V1's history
 * bar - GamePlaying's 0:0 starts with an empty bar, so the wipe's paint on it
 * must not survive the handover): a front grows outward from the centre and any
 * outgoing slot it reaches goes dark, same soft edge ramp as the wipe. Every
 * other slot stays exactly as painted in the hold. On a board with nothing
 * outgoing (V2) the phase is zero-length - totalMs() is unchanged.
 *
 * Which slots are outgoing, and which take a colour fixed by element rather
 * than by x-half (V2's border: its top/bottom segments straddle the seam, so
 * the x-half rule alone would flip two of them), is given by the caller's
 * elementMap + bits at start() - never a hand-kept slot list here.
 *
 * renderFrame() stays clock-free so the host checks can step it.
 */

class LedIntroAnimation : public LedAnimation {
public:
    struct Params {
        uint16_t wipeMs;   ///< outer edge -> centre
        uint16_t holdMs;   ///< full front painted, after the wipe
        uint16_t outMs;    ///< erase outgoing slots outward from the centre, after the hold
        uint16_t edge;     ///< soft ramp inside the moving front, map units (~1 die pitch)
    };

    static Params defaults() {
        return Params{1000, 200, 400, 197};
    }

private:
    Params params;
    CRGB left = CRGB::Black;
    CRGB right = CRGB::Black;
    uint32_t startedMs = 0;
    bool running = false;
    float maxRadius = 0.0f;   ///< farthest live slot from the panel centre

    // Set by start(). outgoing: fades in the out phase (V1's bar; none on V2).
    // colorOverride: 0 none (natural x-half), 1 force left, 2 force right (V2's
    // border top/bottom).
    bool outgoing[Board::LED_COUNT] = {};
    int8_t colorOverride[Board::LED_COUNT] = {};
    float outgoingMinRadius = 0.0f;
    float outgoingMaxRadius = 0.0f;
    bool hasOutgoing = false;

    static float distanceOf(const uint16_t slot) {
        const float x = static_cast<float>(LedSlots::POS[slot][0]);
        const float y = static_cast<float>(LedSlots::POS[slot][1]);
        return sqrtf(x * x + y * y);
    }

public:
    explicit LedIntroAnimation(const Params params = defaults()) : params(params) {
        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            if (LedSlots::POS[slot][0] == LedSlots::SKIP) {
                continue;
            }

            const float distance = distanceOf(slot);
            if (distance > maxRadius) {
                maxRadius = distance;
            }
        }
    }

    void start(
        const uint32_t nowMs, const CRGB leftColor, const CRGB rightColor,
        const uint16_t *elementMap, const uint16_t outgoingBits,
        const uint16_t leftOverrideBits = 0, const uint16_t rightOverrideBits = 0
    ) {
        left = leftColor;
        right = rightColor;
        startedMs = nowMs;
        running = true;

        outgoingMinRadius = 0.0f;
        outgoingMaxRadius = 0.0f;
        hasOutgoing = false;
        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            const uint16_t bits = elementMap[slot];

            outgoing[slot] = (bits & outgoingBits) != 0;
            if (outgoing[slot]) {
                const float distance = distanceOf(slot);
                if (!hasOutgoing || distance < outgoingMinRadius) {
                    outgoingMinRadius = distance;
                }
                if (distance > outgoingMaxRadius) {
                    outgoingMaxRadius = distance;
                }
                hasOutgoing = true;
            }

            if (bits & leftOverrideBits) colorOverride[slot] = 1;
            else if (bits & rightOverrideBits) colorOverride[slot] = 2;
            else colorOverride[slot] = 0;
        }
    }

    void stop() {
        running = false;
    }

    uint32_t totalMs() const {
        return static_cast<uint32_t>(params.wipeMs) + params.holdMs + (hasOutgoing ? params.outMs : 0);
    }

    float getMaxRadius() const {
        return maxRadius;
    }

    bool active(const uint32_t nowMs) const override {
        return running && (nowMs - startedMs) < totalMs();
    }

    void render(const uint32_t nowMs, CRGB *out) const override {
        if (!active(nowMs)) {
            return;
        }

        renderFrame(nowMs - startedMs, out);
    }

    /** Clock-free, so the host checks can step it frame by frame. */
    void renderFrame(const uint32_t elapsedMs, CRGB *out) const {
        const uint32_t holdEnd = static_cast<uint32_t>(params.wipeMs) + params.holdMs;
        const uint32_t clamped = elapsedMs > params.wipeMs ? params.wipeMs : elapsedMs;
        const float front = maxRadius * (1.0f - static_cast<float>(clamped) / static_cast<float>(params.wipeMs));
        const float EDGE = static_cast<float>(params.edge);

        const bool inOutPhase = hasOutgoing && elapsedMs > holdEnd;
        float outFront = 0.0f;
        if (inOutPhase) {
            const uint32_t outElapsed = elapsedMs - holdEnd;
            const uint32_t clampedOut = outElapsed > params.outMs ? params.outMs : outElapsed;
            // From just inside the nearest outgoing slot to the farthest, so the whole
            // phase moves: V1's bar starts ~670 units out, a centre start would idle first.
            const float from = outgoingMinRadius - EDGE;
            outFront = from + (outgoingMaxRadius - from) * (static_cast<float>(clampedOut) / static_cast<float>(params.outMs));
        }

        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            const int16_t x = LedSlots::POS[slot][0];
            if (x == LedSlots::SKIP) {
                continue;
            }

            const LedSlots::Half half = LedSlots::halfOf(x);
            if (half == LedSlots::Half::Seam) {
                continue;
            }

            // Saturated once the front has passed, ramping up over EDGE just ahead of it.
            float intensity = (distanceOf(slot) - front) / EDGE + 1.0f;
            if (intensity <= 0.0f) {
                continue;
            }
            if (intensity > 1.0f || clamped >= params.wipeMs) {
                intensity = 1.0f;
            }

            // Out phase: an outgoing slot fades as the erase front - growing outward
            // from the centre - reaches it, nearest slots first, same soft ramp.
            if (inOutPhase && outgoing[slot]) {
                float outIntensity = (distanceOf(slot) - outFront) / EDGE;
                if (outIntensity < 0.0f) outIntensity = 0.0f;
                if (outIntensity > 1.0f) outIntensity = 1.0f;
                intensity = outIntensity;
            }

            const CRGB tint = colorOverride[slot] == 1
                ? left
                : (colorOverride[slot] == 2 ? right : (half == LedSlots::Half::Left ? left : right));
            out[slot] = CRGB(
                static_cast<uint8_t>(tint.r * intensity),
                static_cast<uint8_t>(tint.g * intensity),
                static_cast<uint8_t>(tint.b * intensity)
            );
        }
    }
};

#endif //LED_INTRO_ANIMATION_H
