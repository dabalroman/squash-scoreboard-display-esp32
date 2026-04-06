#ifndef MODE_SWITCHING_BAR_ADAPTER_H
#define MODE_SWITCHING_BAR_ADAPTER_H

#include <Color.h>
#include "../LedBar.h"

class ModeSwitchingBarAdapter {
    struct Segment {
        uint8_t start;
        uint8_t length;
        Color color;
    };

public:
    static std::array<LedBarPixel, LedBar::PIXEL_COUNT> toLedBarPixels(const uint8_t selectedOption) {
        std::array<LedBarPixel, LedBar::PIXEL_COUNT> pixels = {};

        if (selectedOption >= 3) return pixels;

        // 3 game-mode segments: [7px] gap [8px] gap [7px] = 24px
        constexpr Segment segments[3] = {
            {0,  7, Colors::Green},   // Squash
            {8,  8, Colors::Yellow},  // Volleyball
            {17, 7, Colors::Orange},  // ShortVolleyball
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
