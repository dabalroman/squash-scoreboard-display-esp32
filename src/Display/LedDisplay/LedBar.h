#ifndef BAR_DISPLAY_H
#define BAR_DISPLAY_H

#include <FastLED.h>

#include "LedBarMode.h"

#define BAR_DISPLAY_BLINK_INTERVAL 500
#define BAR_DISPLAY_FIRST_PIXEL_INDEX 88
#define BAR_DISPLAY_AMOUNT_OF_PIXELS 24

struct LedBarPixel {
    CRGB color = CRGB::Black;
    bool isBlinking = false;
};

class LedBar {
    LedBarMode mode = LedBarMode::state;

    CRGB *pixels;
    CRGB celebrationColor = CRGB::RoyalBlue;

    std::array<LedBarPixel, BAR_DISPLAY_AMOUNT_OF_PIXELS> state;

    void renderState(const uint32_t &tickMs) const {
        for (uint8_t i = 0; i < BAR_DISPLAY_AMOUNT_OF_PIXELS; i++) {
            if (state[i].isBlinking && tickMs % BAR_DISPLAY_BLINK_INTERVAL < BAR_DISPLAY_BLINK_INTERVAL / 2) {
                continue;
            }

            pixels[BAR_DISPLAY_FIRST_PIXEL_INDEX + i] = state[i].color;
        }
    }

public:
    explicit LedBar(CRGB *pixels) : pixels(pixels), state() {
    }

    void setState(std::array<LedBarPixel, BAR_DISPLAY_AMOUNT_OF_PIXELS> newState) {
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
        for (uint8_t i = 0; i < BAR_DISPLAY_AMOUNT_OF_PIXELS; i++) {
            const uint8_t brightness = sin8((tickMs / 4 + i * 20) % 255);
            pixels[BAR_DISPLAY_FIRST_PIXEL_INDEX + i] = celebrationColor.scale8(brightness);
        }
    }
};


#endif //BAR_DISPLAY_H
