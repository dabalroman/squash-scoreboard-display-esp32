#ifndef CONFIGVIEW_H
#define CONFIGVIEW_H

#include "DeviceMode/View.h"
#include "Display/GlyphDisplay.h"
#include "RemoteDevelopmentService/LoggerHelper.h"

enum Settings {
    brightness = 0,
    enableAP = 1,
    enableWifi = 2,
    goBack = 3,
};

class ConfigView final : public View {
    PreferencesManager &preferencesManager;

    const char *options[Settings::goBack + 1] = {
        "Brightness",
        "Fallbck AP",
        "WiFi",
        "Return",
    };

    uint8_t selectedOptionId = 0;
    uint8_t optionsListOffset = 0;
    const uint8_t amountOfOptionsOnScreen = 3;
    const uint8_t amountOfOptions = Settings::goBack + 1;

public:
    explicit ConfigView(PreferencesManager &preferencesManager) : preferencesManager(preferencesManager) {
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonA.takeActionIfPossible()) {
            selectedOptionId--;
            queueRender();
        }

        if (remoteInputManager.buttonB.takeActionIfPossible()) {
            selectedOptionId++;
            queueRender();
        }

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

    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        if (!shouldRenderGlyphs) {
            return;
        }

        uint8_t value = 0;
        Color color;

        switch (selectedOptionId) {
            case Settings::brightness:
                value = preferencesManager.settings.brightness;
                color = Color(value, value, value);
                break;
            case Settings::enableWifi:
                value = preferencesManager.settings.enableWifi;
                color = value ? Colors::Green : Colors::Red;
                break;
            case Settings::enableAP:
                value = preferencesManager.settings.enableAp;
                color = value ? Colors::Green : Colors::Red;
                break;
            default:
            case Settings::goBack:
                color = Colors::Black;
                break;
        }

        glyphDisplay.setPlayerAIndicatorAppearance(color);
        glyphDisplay.setPlayerBIndicatorAppearance(color);
        glyphDisplay.display();

        shouldRenderGlyphs = false;
    }

    void renderScreen(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();

        // backDisplay.setCursor(0, BackDisplay::VERTICAL_CURSOR_OFFSET_18pt7b);
        // backDisplay.screen->print("Settings");

        // Indicator
        backDisplay.setCursor(
            0, BackDisplay::VERTICAL_CURSOR_OFFSET_12pt7b * (1 + selectedOptionId - optionsListOffset));
        backDisplay.screen->print(">");

        // Options
        backDisplay.setCursor(BackDisplay::ONE_CHAR_WIDTH_12pt7b, BackDisplay::VERTICAL_CURSOR_OFFSET_12pt7b);
        backDisplay.screen->print(options[optionsListOffset]);

        backDisplay.setCursor(BackDisplay::ONE_CHAR_WIDTH_12pt7b, BackDisplay::VERTICAL_CURSOR_OFFSET_12pt7b * 2);
        backDisplay.screen->print(options[optionsListOffset + 1]);

        backDisplay.setCursor(BackDisplay::ONE_CHAR_WIDTH_12pt7b, BackDisplay::VERTICAL_CURSOR_OFFSET_12pt7b * 3);
        backDisplay.screen->print(options[optionsListOffset + 2]);

        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //CONFIGVIEW_H
