#ifndef LED_SWEEP_ANIMATION_H
#define LED_SWEEP_ANIMATION_H

#include <Arduino.h>
#include <FastLED.h>
#include <math.h>

#include "Board.h"
#include "Display/LedDisplay/Layers/LedAnimation.h"
#include "LedSlotPositions.h"

/**
 * A thin ring that grows from an origin outward across the front LEDs. Two
 * callers, one state machine, on both boards:
 *
 *   boot()         rainbow, one 1 s cycle, driven blocking from setup() by
 *                  LedDisplay::playBootSweep(), screened over a black base.
 *   celebration()  the winner's colour, three 800 ms cycles, a layer over the
 *                  GameOver screen stepped from loop() at ~20 fps. Its origin is
 *                  the winner's half, so the wave breaks from their side.
 *
 * An LedAnimation: it writes only the ring's lit slots into the layer buffer
 * (black, the buffer's fill, is transparent), never into pixels[]. SKIP slots in
 * LedSlotPositions.h are never written. renderFrame() stays clock-free so the
 * host checks can step it.
 */

class LedSweepAnimation : public LedAnimation {
public:
    struct Params {
        uint16_t durationMs;   ///< one cycle, centre -> outer edge
        uint16_t gapMs;        ///< dark pause between cycles; never trails the last one
        uint8_t repeats;
        bool rainbow;          ///< true: hue by radius. false: `solid`, varied by brightness only.
        CRGB solid;
        uint16_t band;         ///< ring half-width in map units - see the Params::band note below
    };

    enum : uint16_t { FRAME_DELAY_MS = 10 };   ///< playBootSweep's frame pacing

    static Params bootParams() {
        return Params{1000, 300, 1, true, CRGB::Black, 690};
    }

    /** Colour is set per win, so the caller fills `solid` before starting. */
    static Params celebrationParams() {
        return Params{800, 300, 3, false, CRGB::Black, 690};
    }

private:
    Params params;
    uint32_t startedMs = 0;
    bool running = false;
    float originX = 0.0f;
    float originY = 0.0f;
    float maxRadius = 0.0f;   ///< farthest live slot from the origin; set with it

    /* Params::band - the ring's half-width in map units, per parameterisation
     * and shared by both boards (V1 and V2 use the same ~197-unit die pitch; do
     * not split it per board). The ring lights a slot while |distance - radius|
     * < band, so it clears a radial gap of G only while band > G/2. Worst gaps
     * measured: V2 254 units from the panel centre, 595 from a half origin; V1
     * 354 from the colon midpoint, 189 from a half centroid - so any band above
     * 298 is safe, and a thinner one blanks whole frames (check_v1/check_v2 guard
     * it). Boot and celebration both use 690 (~3.5 die pitches), a look choice:
     * wide enough to wash over the score it passes (user, 2026-09-24; was 394). */

    void recomputeMaxRadius() {
        maxRadius = 0.0f;

        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            if (LedSlots::POS[slot][0] == LedSlots::SKIP) {
                continue;
            }

            const float dx = static_cast<float>(LedSlots::POS[slot][0]) - originX;
            const float dy = static_cast<float>(LedSlots::POS[slot][1]) - originY;
            const float distance = sqrtf(dx * dx + dy * dy);

            if (distance > maxRadius) {
                maxRadius = distance;
            }
        }
    }

    uint32_t cycleMs() const {
        return static_cast<uint32_t>(params.durationMs) + params.gapMs;
    }

    uint32_t totalMs() const {
        return cycleMs() * params.repeats - params.gapMs;
    }

    /** 6-sector HSV at full S/V. Hand-rolled: the project keeps FastLED's colour
     *  engine unused (no CHSV) so FastLED stays replaceable. */
    static CRGB hueToRgb(const uint8_t hue) {
        const uint8_t sector = hue / 43;
        const uint8_t rise = static_cast<uint8_t>((hue - sector * 43) * 6);
        const uint8_t fall = static_cast<uint8_t>(255 - rise);

        switch (sector) {
            case 0: return CRGB(255, rise, 0);
            case 1: return CRGB(fall, 255, 0);
            case 2: return CRGB(0, 255, rise);
            case 3: return CRGB(0, fall, 255);
            case 4: return CRGB(rise, 0, 255);
            default: return CRGB(255, 0, fall);
        }
    }

public:
    explicit LedSweepAnimation(const Params params) : params(params) {
        recomputeMaxRadius();
    }

    /** Panel centre (the boot sweep) unless a half is chosen. */
    void setOrigin(const float x, const float y) {
        originX = x;
        originY = y;
        recomputeMaxRadius();
    }

    /**
     * Put the origin at the centroid of one half's live slots, so the wave breaks
     * from that player's side rather than the panel centre. Left is x < 0: the
     * border's left column plus digits A and B, which carry the left player's score.
     * x == 0 belongs to neither half - V1's colon dies sit exactly there (V2 has no
     * slot at x == 0, so this is a no-op for it).
     */
    void setOriginToHalf(const bool left) {
        const LedSlots::Half half = left ? LedSlots::Half::Left : LedSlots::Half::Right;
        float sumX = 0.0f;
        float sumY = 0.0f;
        uint16_t count = 0;

        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            if (LedSlots::POS[slot][0] == LedSlots::SKIP) {
                continue;
            }

            const int16_t x = LedSlots::POS[slot][0];
            if (LedSlots::halfOf(x) != half) {
                continue;
            }

            sumX += static_cast<float>(x);
            sumY += static_cast<float>(LedSlots::POS[slot][1]);
            count++;
        }

        if (count == 0) {
            setOrigin(0.0f, 0.0f);
            return;
        }

        setOrigin(sumX / count, sumY / count);
    }

    /** Keeps the origin: boot and celebration share one instance. */
    void setParams(const Params newParams) {
        params = newParams;
    }

    void setSolidColor(const CRGB color) {
        params.solid = color;
    }

    void start(const uint32_t nowMs) {
        startedMs = nowMs;
        running = true;
    }

    void stop() {
        running = false;
    }

    bool active(const uint32_t nowMs) const override {
        return running && (nowMs - startedMs) < totalMs();
    }

    /** One frame at wall-clock `nowMs`; the gap between cycles writes nothing. */
    void render(const uint32_t nowMs, CRGB *out) const override {
        if (!active(nowMs)) {
            return;
        }

        const uint32_t within = (nowMs - startedMs) % cycleMs();

        if (within >= params.durationMs) {
            return;
        }

        renderFrame(within, out);
    }

    /** Clock-free, so the host checks can step it frame by frame. */
    void renderFrame(const uint32_t elapsedMs, CRGB *out) const {
        // The ring must still clear the farthest slot, so the sweep runs one band past it.
        constexpr float HUE_SPAN = 200.0f;   // not 255: the edge must not wrap back to red
        const float BAND = static_cast<float>(params.band);
        const float sweepLen = maxRadius + BAND;

        const uint32_t clamped = elapsedMs > params.durationMs ? params.durationMs : elapsedMs;
        const float radius = sweepLen * static_cast<float>(clamped) / static_cast<float>(params.durationMs);

        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            if (LedSlots::POS[slot][0] == LedSlots::SKIP) {
                continue;
            }

            const float x = static_cast<float>(LedSlots::POS[slot][0]) - originX;
            const float y = static_cast<float>(LedSlots::POS[slot][1]) - originY;
            const float distance = sqrtf(x * x + y * y);
            const float intensity = 1.0f - fabsf(distance - radius) / BAND;

            if (intensity <= 0.0f) {
                continue;
            }

            const CRGB tint = params.rainbow
                                  ? hueToRgb(static_cast<uint8_t>(distance / maxRadius * HUE_SPAN))
                                  : params.solid;

            out[slot] = CRGB(
                static_cast<uint8_t>(tint.r * intensity),
                static_cast<uint8_t>(tint.g * intensity),
                static_cast<uint8_t>(tint.b * intensity)
            );
        }
    }
};

#endif //LED_SWEEP_ANIMATION_H
