#ifndef CONFIGVIEW_H
#define CONFIGVIEW_H

#include "DeviceMode/View.h"
#include "Display/GlyphDisplay.h"
#include "RemoteDevelopmentService/LoggerHelper.h"

enum Settings {
    brightness = 0,
    enableAP = 1,
    enableWifi = 2,
    reboot = 3,
    goBack = 4,
};

class ConfigView final : public View {
    PreferencesManager &preferencesManager;
    std::function<void(DeviceModeState)> onDeviceModeChange;

    const char *options[Settings::goBack + 1] = {
        "Brightness",
        "Fallbck AP",
        "WiFi",
        "Reboot",
        "Return",
    };

    uint8_t selectedOptionId = 0;
    uint8_t optionsListOffset = 0;
    const uint8_t amountOfOptionsOnScreen = 3;
    const uint8_t amountOfOptions = Settings::goBack + 1;

public:
    explicit ConfigView(
        PreferencesManager &preferencesManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange
    )
        : preferencesManager(preferencesManager), onDeviceModeChange(onDeviceModeChange) {
    }

    static uint8_t clamp(const uint8_t value, const uint8_t min, const uint8_t max) {
        if (value < min) {
            return min;
        }

        if (value > max) {
            return max;
        }

        return value;
    }

    void quitConfig(const bool shouldReboot = false) const {
        preferencesManager.save();

        if (shouldReboot) {
            ESP.restart();
        }

        onDeviceModeChange(DeviceModeState::SquashMode);
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

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            switch (selectedOptionId) {
                case Settings::brightness:
                    preferencesManager.settings.brightness =
                            (clamp(preferencesManager.settings.brightness / 32, 1, 8) - 1) * 32 + 31;
                    break;
                case Settings::enableWifi:
                    preferencesManager.settings.enableWifi = !preferencesManager.settings.enableWifi;
                    break;
                case Settings::enableAP:
                    preferencesManager.settings.enableAp = !preferencesManager.settings.enableAp;
                    break;
                default:
                    break;
            }

            queueRender();
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            switch (selectedOptionId) {
                case Settings::brightness:
                    preferencesManager.settings.brightness =
                            clamp(preferencesManager.settings.brightness / 32 + 1, 0, 7) * 32 + 31;
                    break;
                case Settings::enableWifi:
                    preferencesManager.settings.enableWifi = !preferencesManager.settings.enableWifi;
                    break;
                case Settings::enableAP:
                    preferencesManager.settings.enableAp = !preferencesManager.settings.enableAp;
                    break;
                case Settings::reboot:
                    quitConfig(true);
                    break;
                case Settings::goBack:
                    quitConfig(false);
                    break;
                default:
                    break;
            }

            queueRender();
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
                color = Colors::White;
                break;
            case Settings::enableWifi:
                value = preferencesManager.settings.enableWifi;
                color = value ? Colors::Green : Colors::Red;
                break;
            case Settings::enableAP:
                value = preferencesManager.settings.enableAp;
                color = value ? Colors::Green : Colors::Red;
                break;
            case Settings::reboot:
                color = Colors::Red;
                break;
            default:
            case Settings::goBack:
                color = Colors::Black;
                break;
        }

        glyphDisplay.setBrightness(preferencesManager.settings.brightness);
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

        // Indicator
        backDisplay.setCursor(
            0, BackDisplay::VERTICAL_CURSOR_OFFSET_9pt7b * (1 + selectedOptionId - optionsListOffset));
        backDisplay.screen->print(">");

        // Options
        backDisplay.setCursor(BackDisplay::ONE_CHAR_WIDTH_9pt7b, BackDisplay::VERTICAL_CURSOR_OFFSET_9pt7b);
        backDisplay.screen->print(options[optionsListOffset]);

        backDisplay.setCursor(BackDisplay::ONE_CHAR_WIDTH_9pt7b, BackDisplay::VERTICAL_CURSOR_OFFSET_9pt7b * 2);
        backDisplay.screen->print(options[optionsListOffset + 1]);

        backDisplay.setCursor(BackDisplay::ONE_CHAR_WIDTH_9pt7b, BackDisplay::VERTICAL_CURSOR_OFFSET_9pt7b * 3);
        backDisplay.screen->print(options[optionsListOffset + 2]);

        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //CONFIGVIEW_H
