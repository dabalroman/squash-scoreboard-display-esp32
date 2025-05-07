#ifndef BACK_DISPLAY_H
#define BACK_DISPLAY_H

#include "Adafruit_SSD1306.h"
#include "Fonts/FreeMonoBold24pt7b.h"

#define BACK_DISPLAY_BLINK_INTERVAL_MS 1000

class BackDisplay {
    uint32_t tickMs = 0;
    bool isBlinking = false;

    Adafruit_SSD1306 *screen;

    constexpr static uint8_t ONE_CHAR_WIDTH_2x_24p7b = 52;
    constexpr static uint8_t SCROLL_SEPARATOR_WIDTH_2x_24p7b = 104;
    constexpr static uint8_t VERTICAL_CURSOR_OFFSET_2x_24p7b = 59;

public:
    explicit BackDisplay(Adafruit_SSD1306 *backDisplay) : screen(backDisplay) {
        screen->setRotation(2);
        screen->clearDisplay();
        screen->setTextSize(1);
        screen->setTextColor(SSD1306_WHITE);
        screen->setCursor(0, 0);
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

    void renderScore(const uint8_t score) const {
        printCentered(String(score));
    }

    void renderScore(const uint8_t scoreA, const uint8_t scoreB) const {
        char buf[5];
        sprintf(buf, "%02u:%02u", scoreA, scoreB);
        renderScrollingText(buf);
    }

    void renderScrollingText(const String &text, const uint32_t scrollOffsetMs = 0) const {
        const uint16_t textWidth = text.length() * ONE_CHAR_WIDTH_2x_24p7b;
        const uint16_t segmentWidth = textWidth + SCROLL_SEPARATOR_WIDTH_2x_24p7b;
        const uint16_t totalWidth = (textWidth + SCROLL_SEPARATOR_WIDTH_2x_24p7b) * 2;

        const int scrollX =
            ((millis() - scrollOffsetMs) / 25 % segmentWidth) * -1 + ONE_CHAR_WIDTH_2x_24p7b / 2;

        GFXcanvas1 canvas(totalWidth, 64);
        canvas.setFont(&FreeMonoBold24pt7b);
        canvas.setTextSize(2);
        canvas.setCursor(0, VERTICAL_CURSOR_OFFSET_2x_24p7b);
        canvas.print(text);
        canvas.setCursor(segmentWidth, VERTICAL_CURSOR_OFFSET_2x_24p7b);
        canvas.print(text);

        screen->drawBitmap(scrollX, 0, canvas.getBuffer(), totalWidth, 64, WHITE);
    }

    void initForSquashMode() const {
        screen->setTextSize(2);
        screen->setFont(&FreeMonoBold24pt7b);
    }

    void setCursorToCenter(const uint8_t amountOfChars) const {
        setCursor((128 - amountOfChars * ONE_CHAR_WIDTH_2x_24p7b) / 2);
    }

    void setCursor(const uint8_t offset) const {
        screen->setCursor(offset, VERTICAL_CURSOR_OFFSET_2x_24p7b);
    }
};

#endif //BACK_DISPLAY_H
