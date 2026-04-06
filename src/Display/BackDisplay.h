#ifndef BACK_DISPLAY_H
#define BACK_DISPLAY_H

#include "Adafruit_SSD1306.h"
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

public:
    constexpr static uint8_t ONE_CHAR_WIDTH_24pt7b = 26;
    constexpr static uint8_t ONE_CHAR_WIDTH_2x_24pt7b = 52;
    constexpr static uint8_t ONE_CHAR_WIDTH_9pt7b = 13;
    constexpr static uint8_t VERTICAL_CURSOR_OFFSET_24pt7b = 30;
    constexpr static uint8_t VERTICAL_CURSOR_OFFSET_2x_24pt7b = 59;
    constexpr static uint8_t VERTICAL_CURSOR_OFFSET_9pt7b = 20;

    Adafruit_SSD1306 *screen;

    explicit BackDisplay(Adafruit_SSD1306 *backDisplay) : screen(backDisplay) {
        screen->setRotation(2);
        screen->clearDisplay();
        screen->setFont(&FreeMono9pt7b);
        screen->setTextSize(1);
        screen->setTextColor(SSD1306_WHITE);
        screen->setCursor(0, VERTICAL_CURSOR_OFFSET_9pt7b);
        screen->println("Initializing...");
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
        screen->print(text);
    }

    void print(const String &text) const {
        screen->print(text);
    }

    void println(const String &text) const {
        screen->println(text);
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

    void initBigFont() {
        screen->setTextSize(1);
        screen->setFont(&FreeMonoBold24pt7b);
        currentFontDimensions = {ONE_CHAR_WIDTH_24pt7b, VERTICAL_CURSOR_OFFSET_24pt7b};
    }

    void initSmallFont() {
        screen->setTextSize(1);
        screen->setFont(&FreeMono9pt7b);
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
};

#endif //BACK_DISPLAY_H
