#ifndef BAR_DISPLAY_H
#define BAR_DISPLAY_H

#include <FastLED.h>

#include "LedBarMode.h"

struct LedBarPixel {
    CRGB color = CRGB::Black;
    bool isBlinking = false;
};

class LedBar {
    constexpr static uint16_t BLINK_INTERVAL_MS = 500;
    constexpr static uint8_t FIRST_PIXEL_INDEX = 88;

public:
    constexpr static uint8_t PIXEL_COUNT = 24;

private:
    LedBarMode mode = LedBarMode::state;

    CRGB *pixels;
    CRGB celebrationColor = CRGB::RoyalBlue;

    std::array<LedBarPixel, PIXEL_COUNT> state;

    void renderState(const uint32_t &tickMs) const {
        for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
            if (state[i].isBlinking && tickMs % BLINK_INTERVAL_MS < BLINK_INTERVAL_MS / 2) {
                continue;
            }

            pixels[FIRST_PIXEL_INDEX + i] = state[i].color;
        }
    }

public:
    explicit LedBar(CRGB *pixels) : pixels(pixels), state() {
    }

    void setState(std::array<LedBarPixel, PIXEL_COUNT> newState) {
        state = std::move(newState);
    }

    void setCelebrationColor(const CRGB color) {
        celebrationColor = color;
    }

    void setMode(const LedBarMode newMode) {
        mode = newMode;
    }

    void render(const uint32_t &tickMs) const {
        if (mode == LedBarMode::celebration) {
            renderCelebration(tickMs);
            return;
        }

        if (mode == LedBarMode::state) {
            renderState(tickMs);
        }
    }

    void renderCelebration(const uint32_t &tickMs) const {
        for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
            const uint8_t brightness = sin8((tickMs / 4 + i * 20) % 255);
            pixels[FIRST_PIXEL_INDEX + i] = celebrationColor.scale8(brightness);
        }
    }
};


#endif //BAR_DISPLAY_H
