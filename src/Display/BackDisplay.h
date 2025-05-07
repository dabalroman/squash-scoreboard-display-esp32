#ifndef BACK_DISPLAY_H
#define BACK_DISPLAY_H

#include "Adafruit_SSD1306.h"
#include "Fonts/FreeMonoBold24pt7b.h"

#define BACK_DISPLAY_BLINK_INTERVAL_MS 1000

class BackDisplay {
    uint32_t tickMs = 0;
    bool isBlinking = false;

public:
    Adafruit_SSD1306 *screen;

    constexpr static uint8_t ONE_CHAR_WIDTH_2x_24p7b = 52;
    constexpr static uint8_t VERTICAL_CURSOR_OFFSET_2x_24p7b = 59;

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

        if (isBlinking && tickMs % BACK_DISPLAY_BLINK_INTERVAL_MS < BACK_DISPLAY_BLINK_INTERVAL_MS / 2) {
            screen->clearDisplay();
            screen->display();
            return;
        }

        screen->display();
    }

    void setBlinking(const bool isBlinking) {
        this->isBlinking = isBlinking;
    }

    void renderScore(const uint8_t score) const {
        char buf[3];
        sprintf(buf, "%02u", score);
        screen->print(buf);
    }

    void initForSquashMode() const {
        screen->setTextSize(2);
        screen->setFont(&FreeMonoBold24pt7b);
    }

    void setCursorTo2CharCenter() const {
        setCursor(8);
    }

    void setCursor(const uint8_t offset) const {
        screen->setCursor(offset, VERTICAL_CURSOR_OFFSET_2x_24p7b);
    }
};

#endif //BACK_DISPLAY_H
