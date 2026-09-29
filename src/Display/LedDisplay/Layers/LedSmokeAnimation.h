#ifndef LED_SMOKE_ANIMATION_H
#define LED_SMOKE_ANIMATION_H

#include <stdint.h>
#include <FastLED.h>

#include "Board.h"
#include "LedAnimation.h"
#include "../Animation/LedSlotPositions.h"

/**
 * "On fire" smoke for LAYER_2 (task #59): grey wisps of 2-octave value noise,
 * sampled at each slot's physical position and scrolled toward -y, so they
 * rise up the panel (+y is down in LedSlotPositions). Grey only - Screen over
 * the lit digits keeps the player's hue readable underneath.
 *
 * Per-slot lattice coordinates are fixed-point, computed once in the ctor; the
 * per-frame path is integer only (host/device bit-identical, cheap per pixel) and a pure function of
 * nowMs - startedMs.
 */
class LedSmokeAnimation : public LedAnimation {
public:
    static constexpr uint16_t CELL_UNITS = 887;     // ~4.5 die pitches: a wisp spans most of a digit
    static constexpr uint16_t MS_PER_CELL = 1350;   // ~657 units/s; much smaller cells or faster read as flicker
    static constexpr uint8_t PEAK = 150;            // brightest wisp, before Screen

    // Noise below FLOOR is clear air, above CEIL a full wisp.
    static constexpr uint8_t FLOOR = 115;
    static constexpr uint8_t CEIL = 200;

private:
    static constexpr uint32_t NO_SLOT = 0xFFFFFFFFu;
    static constexpr int32_t BIAS = 32768;   // keeps every coordinate positive

    // 8.8 fixed-point lattice coordinates; NO_SLOT marks SKIP.
    uint32_t gx[Board::LED_COUNT];
    uint32_t gy[Board::LED_COUNT];
    uint16_t msPerCell;
    uint8_t peak;
    uint32_t startedMs = 0;
    bool running = false;

    static uint8_t hash8(const uint32_t ix, const uint32_t iy, const uint32_t seed) {
        uint32_t h = ix * 0x27D4EB2Du ^ iy * 0x165667B1u ^ seed;
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        return static_cast<uint8_t>(h >> 24);
    }

    static int32_t lerp(const int32_t a, const int32_t b, const int32_t t) {
        return a + ((b - a) * t) / 255;
    }

public:
    /** 3t^2 - 2t^3 on 0..255, integer. */
    static uint8_t smooth(const uint8_t t) {
        const uint32_t tt = t;
        return static_cast<uint8_t>((tt * tt * (765u - 2u * tt)) / 65025u);
    }

    /** One octave of value noise at 8.8 fixed-point lattice coordinates. */
    static uint8_t sample(const uint32_t fx, const uint32_t fy, const uint32_t seed) {
        const uint32_t ix = fx >> 8, iy = fy >> 8;
        const int32_t tx = smooth(static_cast<uint8_t>(fx & 0xFF));
        const int32_t ty = smooth(static_cast<uint8_t>(fy & 0xFF));
        const int32_t top = lerp(hash8(ix, iy, seed), hash8(ix + 1, iy, seed), tx);
        const int32_t bottom = lerp(hash8(ix, iy + 1, seed), hash8(ix + 1, iy + 1, seed), tx);
        return static_cast<uint8_t>(lerp(top, bottom, ty));
    }

    explicit LedSmokeAnimation(
        const uint16_t cellUnits = CELL_UNITS,
        const uint16_t msPerCell = MS_PER_CELL,
        const uint8_t peak = PEAK
    ) : msPerCell(msPerCell), peak(peak) {
        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            if (LedSlots::POS[slot][0] == LedSlots::SKIP) {
                gx[slot] = gy[slot] = NO_SLOT;
                continue;
            }
            gx[slot] = static_cast<uint32_t>((LedSlots::POS[slot][0] + BIAS) * 256 / cellUnits);
            gy[slot] = static_cast<uint32_t>((LedSlots::POS[slot][1] + BIAS) * 256 / cellUnits);
        }
    }

    void start(const uint32_t nowMs) {
        startedMs = nowMs;
        running = true;
    }

    void stop() {
        running = false;
    }

    bool active(const uint32_t) const override {
        return running;
    }

    void render(const uint32_t nowMs, CRGB *out) const override {
        const uint32_t elapsed = nowMs - startedMs;
        // Split so elapsed * 256 cannot overflow within any real game.
        const uint32_t scroll = (elapsed / msPerCell) * 256u + ((elapsed % msPerCell) * 256u) / msPerCell;
        // Fine octave: double frequency at the same world speed, so detail rides with the
        // wisp - a faster fine octave churns in place and reads as flicker, not scroll.
        const uint32_t fineScroll = scroll * 2u;

        for (uint16_t i = 0; i < Board::LED_COUNT; i++) {
            if (gx[i] == NO_SLOT) continue;

            const uint32_t coarse = sample(gx[i], gy[i] + scroll, 0x9E3779B9u);
            const uint32_t fine = sample(gx[i] * 2u, gy[i] * 2u + fineScroll, 0x85EBCA6Bu);
            const uint32_t n = (coarse * 3u + fine) / 4u;

            uint32_t level = 0;
            if (n > FLOOR) {
                level = n >= CEIL ? 255u : ((n - FLOOR) * 255u) / (CEIL - FLOOR);
                level = (level * level) / 255u;   // soft wisp edges
            }
            const uint8_t v = static_cast<uint8_t>((level * peak) / 255u);
            out[i] = CRGB(v, v, v);
        }
    }
};

#endif //LED_SMOKE_ANIMATION_H
