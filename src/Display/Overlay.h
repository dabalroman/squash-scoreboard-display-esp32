#ifndef OVERLAY_H
#define OVERLAY_H

#include <Arduino.h>

#include "Color.h"
#include "Display/BackDisplay.h"
#include "Display/EInk/EInkDisplay.h"
#include "Display/LedDisplay/LedDisplay.h"

/**
 * What an overlay shows. The strings are copied on show(), so the caller may
 * build them in a local buffer.
 */
struct OverlayContent {
    const char *title;      // e-paper title bar, first OLED line
    const char *line;       // big e-paper line, second OLED line
    LedWord glyphs;         // front LED digits
    Color color;            // LED colour, blinking
    uint32_t durationMs;
};

/**
 * A device-level, full-screen message that takes over all three displays for a
 * while. It knows nothing about DeviceMode, View or any sport: main.cpp owns it,
 * and while it is active it renders instead of `deviceMode->loop()`, so the
 * active mode is paused - no input, no rendering - with its state and timers
 * untouched. Pending work (a score commit) lands on the first frame after.
 *
 * show() replaces whatever is showing. On V1 the e-paper part is the usual stub.
 */
class Overlay {
    enum : uint8_t { TITLE_LEN = 20, LINE_LEN = 12 };

    bool showing = false;
    bool finished = false;
    bool backRendered = false;
    uint32_t endsAtMs = 0;

    char title[TITLE_LEN] = "";
    char line[LINE_LEN] = "";
    LedWord glyphs = {Glyph::Empty, Glyph::Empty, Glyph::Empty, Glyph::Empty};
    Color color = Colors::White;

public:
    void show(const OverlayContent &content, const uint32_t nowMs) {
        snprintf(title, sizeof(title), "%s", content.title != nullptr ? content.title : "");
        snprintf(line, sizeof(line), "%s", content.line != nullptr ? content.line : "");

        glyphs = content.glyphs;

        color = content.color;
        endsAtMs = nowMs + content.durationMs;
        showing = true;
        backRendered = false;
    }

    /**
     * True while the overlay owns the displays. It also ends the overlay on the
     * pass its time is up, so call it once per loop() and branch on the result.
     */
    bool active(const uint32_t nowMs) {
        if (!showing) {
            return false;
        }

        if (static_cast<int32_t>(nowMs - endsAtMs) >= 0) {
            showing = false;
            finished = true;
            return false;
        }

        return true;
    }

    // One-shot: true exactly once, on the pass the overlay ended.
    bool takeFinished() {
        if (!finished) {
            return false;
        }
        finished = false;
        return true;
    }

    void render(LedDisplay &ledDisplay, BackDisplay &backDisplay, EInkDisplay &einkDisplay) {
        ledDisplay.resetAnimations();
        ledDisplay.setColonAppearance();
        ledDisplay.setGlyphsGlyph(glyphs);
        ledDisplay.setGlyphsColor(color, color);
        ledDisplay.setGlyphBlinking(true, true);
        ledDisplay.setPlayersIndicatorsState(true);
        ledDisplay.setIndicatorAppearancePlayerA(color, true);
        ledDisplay.setIndicatorAppearancePlayerB(color, true);
        ledDisplay.setBorderEnabled(true);
        ledDisplay.setBorderAppearance(color, color, true, true);
        ledDisplay.display();

        // The OLED is static for the whole overlay: one I2C frame per show().
        if (!backRendered) {
            backRendered = true;

            backDisplay.clear();
            printBuiltIn(backDisplay, title, 8);
            printBuiltIn(backDisplay, line, 36);
            backDisplay.initSmallFont();
            backDisplay.display();
        }

        einkDisplay.showMessage(title, line);
    }

    /**
     * Put the shared LED state back to something neutral before the view's own
     * initLedDisplay() runs: a view sets colours and glyphs, but not every view
     * turns blinking off.
     */
    static void resetLedState(LedDisplay &ledDisplay) {
        ledDisplay.setGlyphBlinking(false, false);
        ledDisplay.setColonAppearance();
        ledDisplay.setPlayersIndicatorsState(false);
        ledDisplay.setBorderEnabled(false);
        ledDisplay.setBorderAppearance(Colors::Black, Colors::Black);
    }

private:
    // Built-in 6x8 font, centred, doubled while the text still fits 128 px.
    static void printBuiltIn(BackDisplay &backDisplay, const char *text, const int16_t y) {
        const size_t length = strlen(text);
        if (length == 0) {
            return;
        }

        const uint8_t size = length * 12 <= 128 ? 2 : 1;
        const int16_t width = static_cast<int16_t>(length) * 6 * size;

        backDisplay.screen->setFont(nullptr);
        backDisplay.screen->setTextSize(size);
        backDisplay.screen->setCursor((128 - width) / 2, y);
        backDisplay.screen->print(text);
    }
};

#endif //OVERLAY_H
