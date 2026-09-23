#ifndef BAR_DISPLAY_H
#define BAR_DISPLAY_H

#include <FastLED.h>

struct LedBarPixel {
    CRGB color = CRGB::Black;
    bool isBlinking = false;
};

// V1 only. The celebration is now LedSweepAnimation (see LedDisplay::render()'s
// takeover) - this class only ever shows the caller-supplied state.
class LedBar {
    constexpr static uint16_t BLINK_INTERVAL_MS = 500;
    constexpr static uint8_t FIRST_PIXEL_INDEX = 88;

public:
    constexpr static uint8_t PIXEL_COUNT = 24;

private:
    CRGB *pixels;
    std::array<LedBarPixel, PIXEL_COUNT> state;

public:
    explicit LedBar(CRGB *pixels) : pixels(pixels) {
    }

    void setState(std::array<LedBarPixel, PIXEL_COUNT> newState) {
        state = std::move(newState);
    }

    void render(const uint32_t &tickMs) const {
        for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
            if (state[i].isBlinking && tickMs % BLINK_INTERVAL_MS < BLINK_INTERVAL_MS / 2) {
                continue;
            }

            pixels[FIRST_PIXEL_INDEX + i] = state[i].color;
        }
    }
};


#endif //BAR_DISPLAY_H
