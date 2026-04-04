#ifndef BAR_DISPLAY_H
#define BAR_DISPLAY_H

#include <FastLED.h>

#define BAR_DISPLAY_BLINK_INTERVAL 500
#define BAR_DISPLAY_FIRST_PIXEL_INDEX 88
#define BAR_DISPLAY_AMOUNT_OF_PIXELS 24

struct LedBarPixel {
    CRGB color;
    bool isBlinking = false;
};

class LedBar {
    CRGB *pixels;
    std::array<LedBarPixel, BAR_DISPLAY_AMOUNT_OF_PIXELS> state;

public:
    explicit LedBar(CRGB *pixels) : pixels(pixels), state() {
    }

    void setState(std::array<LedBarPixel, BAR_DISPLAY_AMOUNT_OF_PIXELS> newState) {
        state = std::move(newState);
    }

    void render(
        const uint32_t &tickMs
    ) const {
        for (uint8_t i = 0; i < BAR_DISPLAY_AMOUNT_OF_PIXELS; i++) {
            if (state[i].isBlinking && tickMs % BAR_DISPLAY_BLINK_INTERVAL < BAR_DISPLAY_BLINK_INTERVAL / 2) {
                continue;
            }

            pixels[BAR_DISPLAY_FIRST_PIXEL_INDEX + i] = state[i].color;
        }
    }
};


#endif //BAR_DISPLAY_H
