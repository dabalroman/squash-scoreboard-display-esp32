#ifndef LED_BREATHING_ANIMATION_H
#define LED_BREATHING_ANIMATION_H

#include <stdint.h>
#include <math.h>
#include <FastLED.h>

#include "Board.h"
#include "LedAnimation.h"

/**
 * A grey level easing between minLevel and 255 over periodMs, for Multiply:
 * whatever it covers dims and recovers. One cosf per frame, never per pixel
 * (the S2 has no FPU).
 */
class LedBreathingAnimation : public LedAnimation {
    uint16_t periodMs;
    uint8_t minLevel;
    uint32_t startedMs = 0;
    bool running = false;

public:
    explicit LedBreathingAnimation(const uint16_t periodMs = 1400, const uint8_t minLevel = 102)
        : periodMs(periodMs), minLevel(minLevel) {
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

    /** Starts at 255, so switching it on never jumps the screen dark. */
    uint8_t levelAt(const uint32_t nowMs) const {
        const float phase = static_cast<float>((nowMs - startedMs) % periodMs) / static_cast<float>(periodMs);
        const float wave = 0.5f + 0.5f * cosf(6.2831853f * phase);
        const int level = minLevel + static_cast<int>(wave * (255 - minLevel) + 0.5f);
        return static_cast<uint8_t>(level > 255 ? 255 : level);
    }

    void render(const uint32_t nowMs, CRGB *out) const override {
        const uint8_t level = levelAt(nowMs);
        const CRGB grey(level, level, level);

        for (uint16_t i = 0; i < Board::LED_COUNT; i++) {
            out[i] = grey;
        }
    }
};

#endif //LED_BREATHING_ANIMATION_H
