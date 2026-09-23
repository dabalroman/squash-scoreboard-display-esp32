#ifndef MODE_SWITCHING_BAR_ADAPTER_H
#define MODE_SWITCHING_BAR_ADAPTER_H

#include <Color.h>
#include "../LedBar.h"

/**
 * V1 history bar under the mode selector: the bar is split into one segment per
 * visible menu row, left to right in menu order, and the selected row's segment
 * is lit in its colour - so scrolling reads as a position along the bar.
 *
 * The caller passes the row's position among the visible rows and how many there
 * are, never a hand-assigned slot: a slot column kept in the menu table drifted
 * from the menu order and left some rows without a segment.
 */
class ModeSwitchingBarRenderer {
public:
    static std::array<LedBarPixel, LedBar::PIXEL_COUNT> toLedBarPixels(
        const uint8_t slot, const uint8_t slotCount, const Color color
    ) {
        std::array<LedBarPixel, LedBar::PIXEL_COUNT> pixels = {};

        if (slotCount == 0 || slot >= slotCount) return pixels;

        // Equal widths; the last pixel of each is left dark as a gap so adjacent
        // segments read as separate. Any remainder stays dark at the right end.
        const uint8_t width = LedBar::PIXEL_COUNT / slotCount;
        const uint8_t length = width > 1 ? width - 1 : 1;
        const uint8_t start = slot * width;
        const CRGB crgb(color.r, color.g, color.b);

        for (uint8_t i = 0; i < length && start + i < LedBar::PIXEL_COUNT; i++) {
            pixels[start + i].color = crgb;
        }

        return pixels;
    }
};

#endif //MODE_SWITCHING_BAR_ADAPTER_H
