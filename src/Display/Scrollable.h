#ifndef SCROLLABLE_H
#define SCROLLABLE_H

#include <Arduino.h>
#include <vector>

class Scrollable {
    const std::vector<String> &optionsList;

    uint8_t selectedOptionId = 0;
    uint8_t optionsListOffset = 0;
    uint8_t amountOfOptionsOnScreen = 3;
    uint8_t amountOfOptions = 0;

public:
    explicit Scrollable (const std::vector<String> &options): optionsList(options) {
        amountOfOptions = optionsList.size();
    }

    uint8_t getSelectedOptionId() const {
        return selectedOptionId;
    }

    uint8_t getOptionsListOffset() const {
        return optionsListOffset;
    }

    uint8_t getAmountOfOptionsOnScreen() const {
        return amountOfOptionsOnScreen;
    }

    const String &getOptionWithOffset(const uint8_t n = 0) const {
        const uint8_t index = n + optionsListOffset;

        if (index >= amountOfOptions) {
            return optionsList.at(0);
        }

        return optionsList.at(index);
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

        // Rollover to the last option
        if (selectedOptionId > amountOfOptions) {
            selectedOptionId = amountOfOptions - 1;
        }

        if (selectedOptionId >= optionsListOffset + amountOfOptionsOnScreen) {
            optionsListOffset = selectedOptionId - amountOfOptionsOnScreen + 1;
        }

        if (selectedOptionId < optionsListOffset) {
            optionsListOffset = selectedOptionId;
        }

        // Do not allow scrolling past the last option
        if (optionsListOffset > amountOfOptions - amountOfOptionsOnScreen) {
            optionsListOffset = amountOfOptions - amountOfOptionsOnScreen;
        }
    }
};

#endif //SCROLLABLE_H
