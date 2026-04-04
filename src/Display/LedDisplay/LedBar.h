#ifndef BAR_DISPLAY_H
#define BAR_DISPLAY_H

#include <FastLED.h>

#include "Color.h"

class LedBar {
    CRGB *pixels;
    uint32_t lastAnimProgressTickMs = 0;
    uint8_t animProgress = 0;

public:
    explicit LedBar(CRGB *pixels) : pixels(pixels) {
    }

    static Color getColor(uint8_t i) {
        i = i % 8;

        switch (i) {
            default:
            case 0: return Colors::White;
            case 1: return Colors::Green;
            case 2: return Colors::Red;
            case 3: return Colors::Black;
            case 4: return Colors::Blue;
            case 5: return Colors::Yellow;
            case 6: return Colors::Pink;
            case 7: return Colors::Aqua;
        }
    }

    void render(const uint32_t &tickMs) {
        if (lastAnimProgressTickMs + 1000 <= tickMs) {
            animProgress++;
            if (animProgress >= 24) {
                animProgress = 0;
            }

            lastAnimProgressTickMs = tickMs;
        }

        for (uint8_t i = 0; i < 24; i++) {
            const Color color = getColor(i);
            pixels[88 + i] = (i < animProgress)
                ? CRGB(color.r, color.g, color.b)
                : CRGB::Black;
        }

        // 88, 89, 90, 91, 92, 93,
        // 94, 95, 96, 97, 98, 99,
        // 100, 101, 102, 103, 104, 105,
        // 106, 107, 108, 109, 110, 111
    }

    static void setBrightness(const uint8_t brightness) {
        FastLED.setBrightness(brightness);
    }
};


#endif //BAR_DISPLAY_H
