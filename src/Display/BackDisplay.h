#ifndef BACK_DISPLAY_H
#define BACK_DISPLAY_H

#include "Adafruit_SSD1306.h"
#include "Fonts/FreeMono9pt7b.h"
#include "Fonts/FreeMonoBold24pt7b.h"

#define BACK_DISPLAY_BLINK_INTERVAL_MS 1000

struct Dimensions {
    uint8_t width;
    uint8_t height;
};

class BackDisplay {
    constexpr static uint8_t SCROLL_SEPARATOR_WIDTH_2x_24pt7b = 104;

    uint32_t tickMs = 0;
    bool isBlinking = false;
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

    void clear() const {
        screen->clearDisplay();
    }

    void display() {
        tickMs = millis();

        if (isBlinking && tickMs % BACK_DISPLAY_BLINK_INTERVAL_MS < BACK_DISPLAY_BLINK_INTERVAL_MS / 4) {
            screen->clearDisplay();
            screen->display();
            return;
        }

        screen->display();
    }

    void setBlinking(const bool isBlinking) {
        this->isBlinking = isBlinking;
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
        setCursorToLine(0, 0);
        print(String(scoreA));

        const String text = String(scoreB);
        setCursorToLineRightForNumbers(text, 1);
        print(text);

        drawThiccTopToBottomLine(77, 128 - 83, 3);
    }

    void renderPlayerWidget(const String &playerNameA, const String &playerNameB) const {
        setCursorToLine(0, 0);
        print(playerNameA.substring(0, 2));

        const String text = playerNameB.substring(0, 2);
        setCursorToLineRight(text, 1);
        print(text);

        drawThiccTopToBottomLine(77, 128 - 83, 3);
    }

    void renderBigScrollingText(const String &text, const uint32_t scrollOffsetMs = 0) const {
        const uint16_t textWidth = text.length() * ONE_CHAR_WIDTH_2x_24pt7b;
        const uint16_t segmentWidth = textWidth + SCROLL_SEPARATOR_WIDTH_2x_24pt7b;
        const uint16_t totalWidth = (textWidth + SCROLL_SEPARATOR_WIDTH_2x_24pt7b) * 2;

        const int scrollX =
                ((millis() - scrollOffsetMs) / 25 % segmentWidth) * -1 + ONE_CHAR_WIDTH_2x_24pt7b / 2;

        GFXcanvas1 canvas(totalWidth, 64);
        canvas.setFont(&FreeMonoBold24pt7b);
        canvas.setTextSize(2);
        canvas.setCursor(0, VERTICAL_CURSOR_OFFSET_2x_24pt7b);
        canvas.print(text);
        canvas.setCursor(segmentWidth, VERTICAL_CURSOR_OFFSET_2x_24pt7b);
        canvas.print(text);

        screen->drawBitmap(scrollX, 0, canvas.getBuffer(), totalWidth, 64, WHITE);
    }

    void initForSquashMode() {
        screen->setTextSize(1);
        screen->setFont(&FreeMonoBold24pt7b);
        currentFontDimensions = {ONE_CHAR_WIDTH_24pt7b, VERTICAL_CURSOR_OFFSET_24pt7b};
    }

    void initForConfigMode() {
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
