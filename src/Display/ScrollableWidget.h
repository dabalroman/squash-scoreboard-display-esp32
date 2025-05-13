#ifndef SCROLLABLEWIDGET_H
#define SCROLLABLEWIDGET_H
#include "BackDisplay.h"
#include "Scrollable.h"

class ScrollableWidget {
    Scrollable &scrollable;

public:
    explicit ScrollableWidget(Scrollable &scrollable) : scrollable(scrollable) {
    }

    void render(const BackDisplay &backDisplay) const {
        // Indicator
        backDisplay.setCursorFromTopLeft(
            0,
            BackDisplay::VERTICAL_CURSOR_OFFSET_9pt7b *
            (1 + scrollable.getSelectedOption() - scrollable.getOptionsListOffset())
        );
        backDisplay.screen->print(">");

        for (uint8_t i = 0; i < scrollable.getAmountOfOptionsOnScreen(); i++) {
            backDisplay.setCursorFromTopLeft(
                BackDisplay::ONE_CHAR_WIDTH_9pt7b,
                BackDisplay::VERTICAL_CURSOR_OFFSET_9pt7b * (i + 1)
            );
            backDisplay.print(scrollable.getOptionWithOffset(i));
        }
    }
};

#endif //SCROLLABLEWIDGET_H
