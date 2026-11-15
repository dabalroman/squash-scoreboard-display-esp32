#ifndef BACK_DISPLAY_H
#define BACK_DISPLAY_H

#include "Strings.h"
#include "BatteryGauge.h"
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
    uint8_t watchCount = 0;
    bool sameSideMode = false;
    Dimensions currentFontDimensions = {0, 0};
    const GFXfont *currentFont = nullptr;

public:
    constexpr static uint8_t ONE_CHAR_WIDTH_24pt7b = 26;
    constexpr static uint8_t ONE_CHAR_WIDTH_2x_24pt7b = 52;
    constexpr static uint8_t ONE_CHAR_WIDTH_9pt7b = 13;
    constexpr static uint8_t VERTICAL_CURSOR_OFFSET_24pt7b = 30;
    constexpr static uint8_t VERTICAL_CURSOR_OFFSET_2x_24pt7b = 59;
    constexpr static uint8_t VERTICAL_CURSOR_OFFSET_9pt7b = 20;


    Adafruit_SSD1306 *screen;

    explicit BackDisplay(Adafruit_SSD1306 *backDisplay) : screen(backDisplay) {
        screen->setRotation(Board::OLED_ROTATION);
        screen->clearDisplay();
        screen->setTextColor(SSD1306_WHITE);
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

    /**
     * Connected Garmin watches; 0 hides the badge. main.cpp sets it and queues a back
     * render on change, so event-driven views redraw without the badge too.
     */
    void setWatchCount(const uint8_t count) {
        watchCount = count;
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
        screen->print(text);
    }

    void println(const String &text) const {
        screen->println(text);
    }

    /**
     * A full-screen 128x64 PROGMEM bitmap, set bit = lit.
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

    // The OLED battery icon: a 12x7 body, a 1x3 nub, three 2x3 gauge bars (batteryGaugeBars).
    // Unlit bars are left out - an outlined 2 px bar would read as a lit one.
    enum : uint8_t { BATTERY_ICON_WIDTH = 13, BATTERY_ICON_GAP = 3 };

    /**
     * Small right-aligned "85%" in the built-in 5x7 font with a battery icon just left of
     * it, in the top strip above the menu (y 1..7); drawn through OledNavbar, which owns
     * the gate. Restores
     * the current GFX font so the caller's next draw is unaffected.
     */
    void drawBatteryPercent(const uint8_t percent) const {
        char text[6];
        snprintf(text, sizeof(text), "%u%%", percent);
        const int16_t width = static_cast<int16_t>(strlen(text)) * 6 - 1;
        const int16_t textX = 126 - width;
        const int16_t y = 1;

        const int16_t iconX = textX - BATTERY_ICON_GAP - BATTERY_ICON_WIDTH;
        screen->drawRect(iconX, y, BATTERY_ICON_WIDTH - 1, 7, SSD1306_WHITE);
        screen->drawFastVLine(iconX + BATTERY_ICON_WIDTH - 1, y + 2, 3, SSD1306_WHITE);
        const uint8_t bars = batteryGaugeBars(percent);
        for (uint8_t i = 0; i < bars; i++) {
            screen->fillRect(iconX + 2 + i * 3, y + 2, 2, 3, SSD1306_WHITE);
        }

        screen->setFont(nullptr);
        screen->setTextSize(1);
        screen->setCursor(textX, y);
        screen->print(text);
        screen->setFont(currentFont);
    }

    /**
     * Small left-aligned text in the built-in 5x7 font, on the same top strip as
     * drawBatteryPercent; drawn through OledNavbar, which owns the gate.
     */
    void drawTopLeftNote(const char *text) const {
        screen->setFont(nullptr);
        screen->setTextSize(1);
        screen->setCursor(0, 1);
        screen->print(text);
        screen->setFont(currentFont);
    }

    void initBigFont() {
        screen->setTextSize(1);
        screen->setFont(&FreeMonoBold24pt7b);
        currentFont = &FreeMonoBold24pt7b;
        currentFontDimensions = {ONE_CHAR_WIDTH_24pt7b, VERTICAL_CURSOR_OFFSET_24pt7b};
    }

    void initSmallFont() {
        screen->setTextSize(1);
        screen->setFont(&FreeMono9pt7b);
        currentFont = &FreeMono9pt7b;
        currentFontDimensions = {ONE_CHAR_WIDTH_9pt7b, VERTICAL_CURSOR_OFFSET_9pt7b};
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

    // The badge's top-left: on drawBatteryPercent's strip, left of its widest "100%" plus
    // icon (x 87..125); the badge's blanked box ends at x 85. main.cpp feeds a count only
    // on boards without an e-paper.
    enum : uint8_t { WATCH_BADGE_X = 74, WATCH_BADGE_Y = 1 };

    /**
     * The main screen's header, beside the battery percent: a 5x7 watch (strap, case,
     * strap) and the count in the built-in 5x7 font. Nothing when no watch is connected.
     * Drawn through OledNavbar.
     */
    void drawWatchBadge() const {
        if (watchCount == 0) {
            return;
        }

        constexpr uint8_t icon[7] = {0x0E, 0x1F, 0x11, 0x15, 0x11, 0x1F, 0x0E};

        screen->fillRect(WATCH_BADGE_X - 1, WATCH_BADGE_Y - 1, 13, 9, SSD1306_BLACK);
        for (uint8_t row = 0; row < 7; row++) {
            for (uint8_t col = 0; col < 5; col++) {
                if (icon[row] & (0x10 >> col)) {
                    screen->drawPixel(WATCH_BADGE_X + col, WATCH_BADGE_Y + row, SSD1306_WHITE);
                }
            }
        }

        const int16_t cursorX = screen->getCursorX();
        const int16_t cursorY = screen->getCursorY();
        screen->setFont(nullptr);
        screen->setTextSize(1);
        screen->setCursor(WATCH_BADGE_X + 6, WATCH_BADGE_Y);
        screen->print(static_cast<char>('0' + (watchCount > 9 ? 9 : watchCount)));
        screen->setFont(currentFont);
        screen->setCursor(cursorX, cursorY);
    }

private:
};

#endif //BACK_DISPLAY_H
