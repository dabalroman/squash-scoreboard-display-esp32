#ifndef LED_CENTRAL_SCREEN_BORDER_H
#define LED_CENTRAL_SCREEN_BORDER_H

#include <Arduino.h>
#include <FastLED.h>

#include "Board.h"
#include "Color.h"

/**
 * V2: the two LED bars either side of the e-paper, each split in two.
 * Front-facing, so it is a legend for the e-paper rows: top = left court player's
 * colour, bottom = right's. Driven only by LedDisplay::setBorderEnabled /
 * setBorderAppearance - independent of the back-facing indicators, and never
 * sameSideMode-redirected.
 *
 * Not the colon, not the history bar, not the indicators: slots 4 and 9 are the
 * back-facing indicator glyphs. Centre block, verified on the device 2026-09-17:
 * 0,1 left-bottom; 2,3 left-top; 4 indicator; 5,6 right-bottom; 7,8 right-top;
 * 9 indicator. Both sides run bottom -> top.
 *
 * V1 has no border: an empty stub with the same API.
 */

#if BOARD_REV == 2

namespace BorderSlots {
    // Border segment -> absolute slots. Indicators (4, 9) are deliberately absent.
    constexpr uint8_t BOTTOM_LEFT[2] = {0, 1};
    constexpr uint8_t TOP_LEFT[2] = {2, 3};
    constexpr uint8_t BOTTOM_RIGHT[2] = {5, 6};
    constexpr uint8_t TOP_RIGHT[2] = {7, 8};
}

class LedCentralScreenBorder {
    enum : uint16_t { BLINK_INTERVAL_MS = 500 };

    CRGB *pixels;

    CRGB topColor = CRGB::Black;
    CRGB bottomColor = CRGB::Black;
    bool topBlinking = false;
    bool bottomBlinking = false;
    bool enabled = false;

    static bool isBlinkDark(const bool blinking, const uint32_t tickMs) {
        return blinking && tickMs % BLINK_INTERVAL_MS < BLINK_INTERVAL_MS / 2;
    }

    void paint(const uint8_t slots[2], const CRGB color) const {
        pixels[slots[0]] = color;
        pixels[slots[1]] = color;
    }

public:
    explicit LedCentralScreenBorder(CRGB *pixels) : pixels(pixels) {
    }

    void setTop(const Color color, const bool isBlinking) {
        topColor = CRGB(color.r, color.g, color.b);
        topBlinking = isBlinking;
    }

    void setBottom(const Color color, const bool isBlinking) {
        bottomColor = CRGB(color.r, color.g, color.b);
        bottomBlinking = isBlinking;
    }

    void setEnabled(const bool isEnabled) {
        enabled = isEnabled;
    }

    /** Must run every frame: LedDisplay::display() clears the strip first. */
    void render(const uint32_t tickMs) const {
        if (!enabled) {
            return;
        }

        if (!isBlinkDark(topBlinking, tickMs)) {
            paint(BorderSlots::TOP_LEFT, topColor);
            paint(BorderSlots::TOP_RIGHT, topColor);
        }

        if (!isBlinkDark(bottomBlinking, tickMs)) {
            paint(BorderSlots::BOTTOM_LEFT, bottomColor);
            paint(BorderSlots::BOTTOM_RIGHT, bottomColor);
        }
    }
};

#else

class LedCentralScreenBorder {
public:
    explicit LedCentralScreenBorder(CRGB *) {
    }

    void setTop(const Color, const bool) {
    }

    void setBottom(const Color, const bool) {
    }

    void setEnabled(const bool) {
    }

    void render(const uint32_t) const {
    }
};

#endif

#endif //LED_CENTRAL_SCREEN_BORDER_H
