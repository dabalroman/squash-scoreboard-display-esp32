#ifndef MODE_SWITCHING_BAR_ADAPTER_H
#define MODE_SWITCHING_BAR_ADAPTER_H

#include <Color.h>
#include "../LedBar.h"

class ModeSwitchingBarRenderer {
    struct Segment {
        uint8_t start;
        uint8_t length;
        Color color;
    };

public:
    static std::array<LedBarPixel, LedBar::PIXEL_COUNT> toLedBarPixels(const uint8_t selectedOption) {
        std::array<LedBarPixel, LedBar::PIXEL_COUNT> pixels = {};

        if (selectedOption >= 4) return pixels;

        // 4 game-mode segments across 24px
        constexpr Segment segments[4] = {
            {0,  5, Colors::Green},   // Squash
            {6,  5, Colors::Yellow},  // Volleyball
            {12, 5, Colors::Orange},  // ShortVolleyball
            {18, 6, Colors::Blue},    // Padel
        };

        const Segment &seg = segments[selectedOption];
        const CRGB crgb(seg.color.r, seg.color.g, seg.color.b);

        for (uint8_t i = 0; i < seg.length; i++) {
            pixels[seg.start + i].color = crgb;
        }

        return pixels;
    }
};

#endif //MODE_SWITCHING_BAR_ADAPTER_H
