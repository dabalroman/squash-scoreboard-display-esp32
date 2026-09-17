#ifndef SCROLLABLE_H
#define SCROLLABLE_H

#include <Arduino.h>
#include <vector>

/**
 * Menu state shared by every renderer: the options, the selection and the wrap
 * rules. No renderer state lives here - how many rows fit and where the visible
 * window sits is each renderer's own business (the OLED shows 3 rows, the
 * e-paper far more), so both can show different windows of the same list.
 */
class Scrollable {
    const std::vector<String> &optionsList;

    uint8_t selectedOptionId = 0;
    uint8_t amountOfOptions = 0;

public:
    explicit Scrollable (const std::vector<String> &options): optionsList(options) {
        amountOfOptions = optionsList.size();
    }

    uint8_t getSelectedOptionId() const {
        return selectedOptionId;
    }

    uint8_t getOptionCount() const {
        return amountOfOptions;
    }

    const std::vector<String> &getOptions() const {
        return optionsList;
    }

    void cycleSelectedOption(const int8_t offset = 1) {
        selectedOptionId += offset;
        constrainSelectedOption();
    }

    void setSelectedOption(const uint8_t optionId) {
        selectedOptionId = optionId;
        constrainSelectedOption();
    }

protected:
    void constrainSelectedOption() {
        if (selectedOptionId == amountOfOptions) {
            selectedOptionId = 0;
        }

        // Rollover to the last option (uint8_t underflow when cycling back from 0)
        if (selectedOptionId > amountOfOptions) {
            selectedOptionId = amountOfOptions - 1;
        }
    }
};

#endif //SCROLLABLE_H
