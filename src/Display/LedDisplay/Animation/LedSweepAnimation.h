#ifndef LED_SWEEP_ANIMATION_H
#define LED_SWEEP_ANIMATION_H

#include <Arduino.h>
#include <FastLED.h>
#include <math.h>

#include "Board.h"
#include "LedSlotPositions.h"

/**
 * V2: a thin ring that grows from the centre of the e-paper outward across the
 * front LEDs. Two callers, one state machine:
 *
 *   boot()         rainbow, one 1 s cycle, driven blocking from setup() so it
 *                  plays against the e-paper splash.
 *   celebration()  the winner's colour, three 1 s cycles, stepped from loop()
 *                  at ~20 fps while the GameOver view holds the screen. Its origin
 *                  is the winner's half, so the wave breaks from their side.
 *
 * play() is only a blocking driver over start()/active()/render() - there is no
 * second copy of the cycle logic. renderFrame() stays clock-free so the host
 * checks can step it.
 *
 * The animation owns every non-SKIP slot: it writes black where the ring is not,
 * so a frame never inherits the previous one. Slots marked SKIP in
 * LedSlotPositions.h (back indicators 4/9, dead slots 12/28/44/60) are left
 * untouched, so whatever else drew them survives.
 *
 * V1 has no sweep: an empty stub with the same API.
 */

#if BOARD_REV == 2

class LedSweepAnimation {
public:
    struct Params {
        uint16_t durationMs;   ///< one cycle, centre -> outer edge
        uint16_t gapMs;        ///< dark pause between cycles; never trails the last one
        uint8_t repeats;
        bool rainbow;          ///< true: hue by radius. false: `solid`, varied by brightness only.
        CRGB solid;
    };

    enum : uint16_t { FRAME_DELAY_MS = 10 };

    static Params bootParams() {
        return Params{1000, 300, 1, true, CRGB::Black};
    }

    /** Colour is set per win, so the caller fills `solid` before starting. */
    static Params celebrationParams() {
        return Params{1000, 300, 3, false, CRGB::Black};
    }

private:
    CRGB *pixels;
    Params params;
    uint32_t startedMs = 0;
    bool running = false;
    float originX = 0.0f;
    float originY = 0.0f;
    float maxRadius = 0.0f;   ///< farthest live slot from the origin; set with it

    /** Two LED pitches. The ring lights a slot while |distance - radius| < BAND, so it
     *  clears a radial gap of G only while BAND > G/2. The worst gap is 254 units from
     *  the panel centre but 595 from a half origin (the far side's dies bunch up at
     *  similar radii), needing BAND > 298 - so 394 covers both. A thinner ring falls into
     *  a gap and blanks whole frames; check_v2 guards it. Function-local at every use:
     *  a static constexpr member is an ODR link error on GCC 8.4. */
    static float band() { return 394.0f; }

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
    LedSweepAnimation(CRGB *pixels, const Params params) : pixels(pixels), params(params) {
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
     */
    void setOriginToHalf(const bool left) {
        float sumX = 0.0f;
        float sumY = 0.0f;
        uint16_t count = 0;

        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            if (LedSlots::POS[slot][0] == LedSlots::SKIP) {
                continue;
            }

            const bool onLeft = LedSlots::POS[slot][0] < 0;
            if (onLeft != left) {
                continue;
            }

            sumX += static_cast<float>(LedSlots::POS[slot][0]);
            sumY += static_cast<float>(LedSlots::POS[slot][1]);
            count++;
        }

        if (count == 0) {
            setOrigin(0.0f, 0.0f);
            return;
        }

        setOrigin(sumX / count, sumY / count);
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

    bool active(const uint32_t nowMs) const {
        return running && (nowMs - startedMs) < totalMs();
    }

    /** Black on every non-SKIP slot. Used for the gap between cycles. */
    void blank() const {
        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            if (LedSlots::POS[slot][0] == LedSlots::SKIP) {
                continue;
            }

            pixels[slot] = CRGB::Black;
        }
    }

    /** One frame at wall-clock `nowMs`, picking the cycle and the offset within it. */
    void render(const uint32_t nowMs) const {
        if (!running) {
            return;
        }

        const uint32_t elapsed = nowMs - startedMs;

        if (elapsed >= totalMs()) {
            blank();
            return;
        }

        const uint32_t within = elapsed % cycleMs();

        if (within >= params.durationMs) {
            blank();
            return;
        }

        renderFrame(within);
    }

    /** Clock-free, so the host checks can step it frame by frame. */
    void renderFrame(const uint32_t elapsedMs) const {
        // The ring must still clear the farthest slot, so the sweep runs one band past it.
        constexpr float HUE_SPAN = 200.0f;   // not 255: the edge must not wrap back to red
        const float BAND = band();
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
                pixels[slot] = CRGB::Black;
                continue;
            }

            const CRGB tint = params.rainbow
                                  ? hueToRgb(static_cast<uint8_t>(distance / maxRadius * HUE_SPAN))
                                  : params.solid;

            pixels[slot] = CRGB(
                static_cast<uint8_t>(tint.r * intensity),
                static_cast<uint8_t>(tint.g * intensity),
                static_cast<uint8_t>(tint.b * intensity)
            );
        }
    }

    /** Blocking driver over the same state machine. Boot only - never call it from loop(). */
    void play() {
        start(millis());

        while (active(millis())) {
            render(millis());
            FastLED.show();
            delay(FRAME_DELAY_MS);
        }

        stop();
        FastLED.clear();
        FastLED.show();
    }
};

#else

class LedSweepAnimation {
public:
    struct Params {
        uint16_t durationMs;
        uint16_t gapMs;
        uint8_t repeats;
        bool rainbow;
        CRGB solid;
    };

    enum : uint16_t { FRAME_DELAY_MS = 0 };

    static Params bootParams() { return Params{0, 0, 0, false, CRGB::Black}; }
    static Params celebrationParams() { return Params{0, 0, 0, false, CRGB::Black}; }

    LedSweepAnimation(CRGB *, const Params) {
    }

    void setSolidColor(const CRGB) {
    }

    void setOrigin(const float, const float) {
    }

    void setOriginToHalf(const bool) {
    }

    void start(const uint32_t) {
    }

    void stop() {
    }

    bool active(const uint32_t) const { return false; }

    void blank() const {
    }

    void render(const uint32_t) const {
    }

    void renderFrame(const uint32_t) const {
    }

    void play() {
    }
};

#endif

#endif //LED_SWEEP_ANIMATION_H
