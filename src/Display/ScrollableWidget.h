#ifndef SCROLLABLEWIDGET_H
#define SCROLLABLEWIDGET_H
#include "BackDisplay.h"
#include "Scrollable.h"

/**
 * OLED renderer for a Scrollable. Owns its own window: 3 rows and a scroll offset
 * that only moves as far as needed to keep the selection visible.
 */
class ScrollableWidget {
    // Baselines 24/42/60: row 0 clears the navbar strip (y 1..7) and the dead V2 rows.
    enum : uint8_t { ROWS_ON_SCREEN = 3, ROW_PITCH = 18, TOP_OFFSET = 6 };

    static uint8_t rowBaseline(const uint8_t row) {
        return TOP_OFFSET + ROW_PITCH * (row + 1);
    }

    Scrollable &scrollable;
    mutable uint8_t optionsListOffset = 0;

    void updateWindow() const {
        const uint8_t selectedOptionId = scrollable.getSelectedOptionId();
        const uint8_t amountOfOptions = scrollable.getOptionCount();

        if (selectedOptionId >= optionsListOffset + ROWS_ON_SCREEN) {
            optionsListOffset = selectedOptionId - ROWS_ON_SCREEN + 1;
        }

        if (selectedOptionId < optionsListOffset) {
            optionsListOffset = selectedOptionId;
        }

        // Do not allow scrolling past the last option
        if (amountOfOptions <= ROWS_ON_SCREEN) {
            optionsListOffset = 0;
        } else if (optionsListOffset > amountOfOptions - ROWS_ON_SCREEN) {
            optionsListOffset = amountOfOptions - ROWS_ON_SCREEN;
        }
    }

    const String &optionOnRow(const uint8_t row) const {
        const uint8_t index = row + optionsListOffset;

        if (index >= scrollable.getOptionCount()) {
            return scrollable.getOptions().at(0);
        }

        return scrollable.getOptions().at(index);
    }

public:
    explicit ScrollableWidget(Scrollable &scrollable) : scrollable(scrollable) {
    }

    void render(const BackDisplay &backDisplay) const {
        updateWindow();

        // Indicator
        backDisplay.setCursorFromTopLeft(
            0,
            rowBaseline(scrollable.getSelectedOptionId() - optionsListOffset)
        );
        backDisplay.print(">");

        for (uint8_t i = 0; i < ROWS_ON_SCREEN; i++) {
            backDisplay.setCursorFromTopLeft(
                BackDisplay::ONE_CHAR_WIDTH_9pt7b,
                rowBaseline(i)
            );
            backDisplay.print(optionOnRow(i));
        }
    }
};

#endif //SCROLLABLEWIDGET_H
