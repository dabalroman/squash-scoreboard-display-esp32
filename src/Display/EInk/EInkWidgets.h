#ifndef EINK_WIDGETS_H
#define EINK_WIDGETS_H

/**
 * The e-paper's shared chrome: layout geometry, text placement helpers, and the
 * four drawable pieces every screen reuses - the title bar, the footer, a
 * tickbox and a battery icon.
 *
 * Included only from EInkDisplay.h's `BOARD_REV == 2` branch, so it needs no
 * `#if BOARD_REV` of its own and V1 never sees the GFX fonts it pulls in.
 *
 * Free `inline` functions, not a class: they are stateless drawing over a canvas
 * the caller owns, and a `static constexpr` member would be an ODR link error on
 * GCC 8.4 anyway.
 */

#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

#include "EInkAsync.h"

// E-paper layout, 128 x 296 portrait. Its own constants - nothing from the OLED.
namespace EInkLayout {
    constexpr int16_t TITLE_HEIGHT = 30;
    constexpr int16_t TITLE_BASELINE = 22;
    constexpr int16_t ROWS_TOP = 36;
    constexpr int16_t ROW_PITCH = 32;
    constexpr int16_t ROW_BASELINE = 23;     // from the row top, FreeSans12pt
    // Three heights, one per line count. All stay <= 62 px, which is what keeps
    // visibleRows() at 6 for every one of them - so adding or dropping a footer
    // line never re-flows the menu above it.
    constexpr int16_t FOOTER_HEIGHT = 31;          // rule + padding + one 12 pt line
    constexpr int16_t FOOTER_HEIGHT_TWO = 48;      // ...plus a 9 pt line
    constexpr int16_t FOOTER_HEIGHT_THREE = 62;    // ...plus another
    constexpr int16_t FOOTER_BASELINE = 24;        // line 1, 12 pt, from the footer top
    constexpr int16_t FOOTER_EXTRA_BASELINE = 41;  // line 2, 9 pt
    constexpr int16_t FOOTER_EXTRA_PITCH = 16;     // line 3 and on
    constexpr int16_t CHECKBOX_SIZE = 13;
    constexpr int16_t SELECTION_BORDER = 2;  // outline thickness of the selected row
    constexpr int16_t TEXT_MARGIN = 5;
    constexpr int16_t BATTERY_ICON_WIDTH = 30;
    constexpr int16_t BATTERY_ICON_HEIGHT = 16;
    constexpr int16_t BATTERY_BARS = 3;
}

namespace EInkWidgets {
    enum : uint16_t { PAPER = EInkAsync::PAPER, INK = EInkAsync::INK };

    inline int16_t textWidth(GFXcanvas1 &g, const char *text, const GFXfont *font) {
        g.setFont(font);
        g.setTextSize(1);
        int16_t x1, y1;
        uint16_t w, h;
        g.getTextBounds(text, 0, 100, &x1, &y1, &w, &h);
        return static_cast<int16_t>(w);
    }

    inline void printCentered(GFXcanvas1 &g, const char *text, const int16_t baseline,
                              const GFXfont *font, const uint8_t size = 1) {
        g.setFont(font);
        g.setTextSize(size);
        int16_t x1, y1;
        uint16_t w, h;
        g.getTextBounds(text, 0, baseline, &x1, &y1, &w, &h);
        g.setCursor((g.width() - static_cast<int16_t>(w)) / 2 - x1, baseline);
        g.print(text);
    }

    inline void printAt(GFXcanvas1 &g, const char *text, const int16_t x,
                        const int16_t baseline, const GFXfont *font) {
        g.setFont(font);
        g.setTextSize(1);
        g.setCursor(x, baseline);
        g.print(text);
    }

    inline void printRightAligned(GFXcanvas1 &g, const char *text, const int16_t rightEdge,
                                  const int16_t baseline, const GFXfont *font) {
        printAt(g, text, rightEdge - textWidth(g, text, font), baseline, font);
    }

    // The inverted title bar, on every menu and message screen.
    inline void drawHeader(GFXcanvas1 &g, const char *title) {
        g.fillRect(0, 0, g.width(), EInkLayout::TITLE_HEIGHT, INK);
        g.setTextColor(PAPER);
        printCentered(g, title, EInkLayout::TITLE_BASELINE, &FreeSansBold12pt7b);
    }

    // A square outline with an inset fill when ticked. Used by menu rows and by
    // the player selectors for in/out membership.
    inline void drawTickbox(GFXcanvas1 &g, const int16_t x, const int16_t y,
                            const int16_t size, const bool ticked) {
        g.drawRect(x, y, size, size, INK);
        if (ticked) {
            g.fillRect(x + 3, y + 3, size - 6, size - 6, INK);
        }
    }

    /**
     * Outline, nub, and a coarse three-segment gauge inside it: > 80 % is three
     * bars, > 50 % two, > 20 % one, and at or below 20 % none. Deliberately coarse
     * - the percent printed beside it carries the exact value, and a finer gauge
     * would only invite reading a number off the picture that the text already
     * gives. A segment is drawn full or as an outline, never partially.
     */
    inline void drawBatteryIcon(GFXcanvas1 &g, const int16_t x, const int16_t y, const int16_t percent) {
        constexpr int16_t bodyWidth = EInkLayout::BATTERY_ICON_WIDTH - 3;
        g.drawRect(x, y, bodyWidth, EInkLayout::BATTERY_ICON_HEIGHT, INK);
        g.fillRect(x + bodyWidth, y + 4, 3, EInkLayout::BATTERY_ICON_HEIGHT - 8, INK);

        const int16_t filled = percent > 80 ? 3 : (percent > 50 ? 2 : (percent > 20 ? 1 : 0));

        constexpr int16_t barWidth = 6;
        constexpr int16_t barGap = 2;
        constexpr int16_t barHeight = EInkLayout::BATTERY_ICON_HEIGHT - 4;
        for (int16_t i = 0; i < EInkLayout::BATTERY_BARS; i++) {
            const int16_t barX = x + 2 + i * (barWidth + barGap);
            if (i < filled) {
                g.fillRect(barX, y + 2, barWidth, barHeight, INK);
            } else {
                g.drawRect(barX, y + 2, barWidth, barHeight, INK);
            }
        }
    }

    inline void drawBatteryReading(GFXcanvas1 &g, const int16_t baseline, const int16_t percent) {
        char text[6];
        snprintf(text, sizeof(text), "%d%%", percent);

        constexpr int16_t gap = 5;
        const int16_t total = EInkLayout::BATTERY_ICON_WIDTH + gap + textWidth(g, text, &FreeSans12pt7b);
        const int16_t x = (g.width() - total) / 2;

        drawBatteryIcon(g, x, baseline - EInkLayout::BATTERY_ICON_HEIGHT, percent);
        printAt(g, text, x + EInkLayout::BATTERY_ICON_WIDTH + gap, baseline, &FreeSans12pt7b);
    }

    /**
     * The footer: a rule, then up to three centred lines - a 12 pt headline (the
     * battery reading when there is one, otherwise `line1`) and up to two 9 pt
     * lines under it.
     *
     * Everything is centred and gets a line to itself because 128 px will not hold
     * two of these side by side: a 12 pt "100%" beside a right-aligned firmware
     * version overlapped.
     *
     * `extra` entries that are null or empty are skipped, so a missing IP simply
     * makes the footer one line shorter instead of leaving a gap.
     */
    inline void drawFooter(GFXcanvas1 &g, const int16_t top, const char *line1,
                           const char *const *extra, const uint8_t extraCount,
                           const int16_t batteryPercent) {
        g.fillRect(0, top, g.width(), 2, INK);
        g.setTextColor(INK);

        const int16_t baseline = top + EInkLayout::FOOTER_BASELINE;

        if (batteryPercent >= 0) {
            drawBatteryReading(g, baseline, batteryPercent);
        } else if (line1 != nullptr && line1[0] != 0) {
            printCentered(g, line1, baseline, &FreeSans12pt7b);
        }

        for (uint8_t i = 0; i < extraCount; i++) {
            printCentered(g, extra[i],
                          top + EInkLayout::FOOTER_EXTRA_BASELINE + i * EInkLayout::FOOTER_EXTRA_PITCH,
                          &FreeSans9pt7b);
        }
    }
}

#endif //EINK_WIDGETS_H
