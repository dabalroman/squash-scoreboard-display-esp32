#ifndef BACK_DISPLAY_H
#define BACK_DISPLAY_H

#include "Strings.h"
#include "Adafruit_SSD1306.h"
#include "Board.h"
#include "Fonts/FreeMono9pt7b.h"
#include "Fonts/FreeMonoBold24pt7b.h"

struct Dimensions {
    uint8_t width;
    uint8_t height;
};

class BackDisplay {
    constexpr static uint16_t BLINK_INTERVAL_MS = 1000;
    constexpr static uint8_t SCROLL_SEPARATOR_WIDTH_2x_24pt7b = 104;

    uint32_t tickMs = 0;
    bool isBlinking = false;
    bool sameSideMode = false;
    Dimensions currentFontDimensions = {0, 0};
    uint8_t currentFontAscent = 0;
    const GFXfont *currentFont = nullptr;

public:
    constexpr static uint8_t ONE_CHAR_WIDTH_24pt7b = 26;
    constexpr static uint8_t ONE_CHAR_WIDTH_2x_24pt7b = 52;
    constexpr static uint8_t ONE_CHAR_WIDTH_9pt7b = 13;
    constexpr static uint8_t VERTICAL_CURSOR_OFFSET_24pt7b = 30;
    constexpr static uint8_t VERTICAL_CURSOR_OFFSET_2x_24pt7b = 59;
    constexpr static uint8_t VERTICAL_CURSOR_OFFSET_9pt7b = 20;

    /**
     * First row text may occupy: Board::OLED_DEAD_TOP_ROWS, 11 on V2 and 0 on V1.
     * The V2 bench panel is damaged. The damage is not a
     * solid strip: every *even* row from 0 to 12 is dead (alternating COM lines),
     * measured on the device 2026-09-18. 13 would clear it completely, but 11 is
     * the chosen floor - only the stripe at row 12 then crosses a glyph, and one
     * missing line is hard to notice, while the two rows saved keep the 3-row menu
     * spacing closer to even.
     *
     * Any text that would land above is pushed down by exactly the deficit; 0
     * restores the original layout. V1's healthy panel has 0, which frees its top
     * strip for drawBatteryPercent().
     */
    constexpr static uint8_t DEAD_TOP_ROWS = Board::OLED_DEAD_TOP_ROWS;

    Adafruit_SSD1306 *screen;

    explicit BackDisplay(Adafruit_SSD1306 *backDisplay) : screen(backDisplay) {
        screen->setRotation(Board::OLED_ROTATION);
        screen->clearDisplay();
        screen->setTextColor(SSD1306_WHITE);
        // Through initSmallFont, so the ascent used by clearDeadTop is set before
        // the first draw rather than left at 0.
        initSmallFont();
        setCursorToLine();
        println(Str::BOOT_OLED_INITIALIZING);
        screen->display();
    }

    void setSameSideMode(const bool sameSide) {
        sameSideMode = sameSide;
    }

    void clear() const {
        screen->clearDisplay();
    }

    void display() {
        tickMs = millis();

        if (isBlinking && tickMs % BLINK_INTERVAL_MS < BLINK_INTERVAL_MS / 4) {
            screen->clearDisplay();
            screen->display();
            return;
        }

        screen->display();
    }

    void setBlinking(const bool newIsBlinking) {
        this->isBlinking = newIsBlinking;
    }

    void printCentered(const String &text) const {
        setCursorToCenter(text.length());
        print(text);
    }

    void print(const String &text) const {
        clearDeadTop();
        screen->print(text);
    }

    void println(const String &text) const {
        clearDeadTop();
        screen->println(text);
    }

    /**
     * A full-screen 128x64 PROGMEM bitmap, set bit = lit. Deliberately ignores
     * DEAD_TOP_ROWS: artwork uses the whole panel (user's call, 2026-09-28).
     */
    void drawBitmap(const uint8_t *bitmap) const {
        if (bitmap == nullptr) {
            return;
        }

        screen->drawBitmap(0, 0, bitmap, 128, 64, SSD1306_WHITE);
    }

    void drawThiccTopToBottomLine(const uint8_t x1, const uint8_t x2, const uint8_t thiccness = 3) const {
        const uint8_t half = thiccness / 2;
        for (int8_t i = -half; i <= half; i++) {
            screen->drawLine(x1 + i, 0, x2 + i, 64, WHITE);
        }
    }

    void renderScoreWidget(const uint8_t scoreA, const uint8_t scoreB) const {
        const uint8_t left  = sameSideMode ? scoreB : scoreA;
        const uint8_t right = sameSideMode ? scoreA : scoreB;

        setCursorToLine(0, 0);
        print(String(left));

        const String text = String(right);
        setCursorToLineRightForNumbers(text, 1);
        print(text);

        drawThiccTopToBottomLine(77, 128 - 83, 3);
    }

    void renderScoreWidget(const String &scoreA, const String &scoreB) const {
        const String &left  = sameSideMode ? scoreB : scoreA;
        const String &right = sameSideMode ? scoreA : scoreB;

        setCursorToLine(0, 0);
        print(left);

        setCursorToLineRightForNumbers(right, 1);
        print(right);

        drawThiccTopToBottomLine(77, 128 - 83, 3);
    }

    void renderPlayerWidget(const String &playerNameA, const String &playerNameB) const {
        const String &left  = sameSideMode ? playerNameB : playerNameA;
        const String &right = sameSideMode ? playerNameA : playerNameB;

        setCursorToLine(0, 0);
        print(left.substring(0, 2));

        const String text = right.substring(0, 2);
        setCursorToLineRight(text, 1, 3);
        print(text);

        drawThiccTopToBottomLine(77, 128 - 83, 3);
    }

    /**
     * Small right-aligned "85%" in the built-in 5x7 font, in the top strip above the
     * menu's first row (row 0 of the 9 pt menu starts at y 10, this spans y 1..7).
     * Only meaningful where that strip is lit (DEAD_TOP_ROWS == 0). Restores the
     * current GFX font so the caller's next draw is unaffected.
     */
    void drawBatteryPercent(const uint8_t percent) const {
        char text[6];
        snprintf(text, sizeof(text), "%u%%", percent);
        const int16_t width = static_cast<int16_t>(strlen(text)) * 6 - 1;

        screen->setFont(nullptr);
        screen->setTextSize(1);
        screen->setCursor(126 - width, DEAD_TOP_ROWS + 1);
        screen->print(text);
        screen->setFont(currentFont);
    }

    void initBigFont() {
        screen->setTextSize(1);
        screen->setFont(&FreeMonoBold24pt7b);
        currentFont = &FreeMonoBold24pt7b;
        currentFontDimensions = {ONE_CHAR_WIDTH_24pt7b, VERTICAL_CURSOR_OFFSET_24pt7b};
        currentFontAscent = measureAscent();
    }

    void initSmallFont() {
        screen->setTextSize(1);
        screen->setFont(&FreeMono9pt7b);
        currentFont = &FreeMono9pt7b;
        currentFontDimensions = {ONE_CHAR_WIDTH_9pt7b, VERTICAL_CURSOR_OFFSET_9pt7b};
        currentFontAscent = measureAscent();
    }

    void setCursorToCenter(const uint8_t amountOfChars) const {
        setCursorFromTopLeft((128 - amountOfChars * ONE_CHAR_WIDTH_2x_24pt7b) / 2);
    }

    void setCursorFromTopLeft(const uint8_t x, const uint8_t y = VERTICAL_CURSOR_OFFSET_24pt7b) const {
        screen->setCursor(x, y);
    }

    void setCursorToLine(const uint8_t charOffset = 0, const uint8_t line = 0) const {
        screen->setCursor(charOffset * currentFontDimensions.width, (line + 1) * currentFontDimensions.height);
    }

    void setCursorToLineRight(const String &text, const uint8_t line = 0, const uint8_t offset = 2) const {
        int16_t x1, y1;
        uint16_t w, h;
        screen->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
        screen->setCursor(127 - offset - w, (line + 1) * currentFontDimensions.height);
    }

    void setCursorToLineRightForNumbers(const String &text, const uint8_t line = 0, const uint8_t offset = 4) const {
        setCursorToLineRight(text, line, offset);
    }

private:
    /**
     * Tallest ascent among the glyphs these screens actually draw, for the font
     * that is currently set. Measured once per font switch, from one sample
     * string, rather than from the font's whole table: the table's worst case
     * comes from punctuation nothing here uses and would drop every line 3 px
     * further than needed.
     */
    uint8_t measureAscent() const {
        int16_t x1, y1;
        uint16_t w, h;
        screen->getTextBounds(
            F("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"),
            0, 0, &x1, &y1, &w, &h
        );

        return y1 < 0 ? static_cast<uint8_t>(-y1) : 0;
    }

    /**
     * Push the cursor down so the line about to be drawn starts at or below
     * DEAD_TOP_ROWS.
     *
     * Applied here, at print time, rather than in each cursor setter: this is the
     * one point every draw passes through, so no call site can land in the dead
     * rows by using a setter that was missed. The drop comes from the font's
     * ascent, not the individual string - fitting each string exactly made the top
     * score hop 1-2 px as its value changed ('1' and '4' are shorter than the
     * other digits) and put the menu's ">" marker a pixel off its label. A
     * `println` block inherits the same offset on its following lines.
     *
     * With no GFX font set the ascent is 0 and the cursor y is already the glyph
     * top, so this degrades to a plain floor - which is correct for that font too.
     */
    void clearDeadTop() const {
        if (DEAD_TOP_ROWS == 0) {
            return;
        }

        const int16_t y = screen->getCursorY();
        const int16_t top = y - currentFontAscent;

        if (top < DEAD_TOP_ROWS) {
            screen->setCursor(screen->getCursorX(), y + (DEAD_TOP_ROWS - top));
        }
    }
};

#endif //BACK_DISPLAY_H
