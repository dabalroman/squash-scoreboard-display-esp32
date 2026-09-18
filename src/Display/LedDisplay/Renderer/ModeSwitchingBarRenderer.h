#ifndef MODE_SWITCHING_BAR_ADAPTER_H
#define MODE_SWITCHING_BAR_ADAPTER_H

#include <Color.h>
#include "../LedBar.h"

/**
 * V1 history bar under the mode selector: one segment per sport, lit for the
 * selected one. The slot and the colour both come from the caller's menu table -
 * the renderer holds no idea of what a mode is, so reordering or adding a menu
 * entry cannot leave the bar showing a different sport's colour than the glyphs.
 */
class ModeSwitchingBarRenderer {
    struct Segment {
        uint8_t start;
        uint8_t length;
    };

public:
    static constexpr uint8_t SLOT_COUNT = 4;

    // barSlot < 0 (a non-sport entry, e.g. Config) leaves the bar dark.
    static std::array<LedBarPixel, LedBar::PIXEL_COUNT> toLedBarPixels(const int8_t barSlot, const Color color) {
        std::array<LedBarPixel, LedBar::PIXEL_COUNT> pixels = {};

        if (barSlot < 0 || barSlot >= static_cast<int8_t>(SLOT_COUNT)) return pixels;

        // 4 segments across 24px
        constexpr Segment segments[SLOT_COUNT] = {
            {0,  5},
            {6,  5},
            {12, 5},
            {18, 6},
        };

        const Segment &seg = segments[barSlot];
        const CRGB crgb(color.r, color.g, color.b);

        for (uint8_t i = 0; i < seg.length; i++) {
            pixels[seg.start + i].color = crgb;
        }

        return pixels;
    }
};

#endif //MODE_SWITCHING_BAR_ADAPTER_H
