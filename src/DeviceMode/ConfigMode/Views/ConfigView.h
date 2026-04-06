#ifndef CONFIGVIEW_H
#define CONFIGVIEW_H

#include "DeviceMode/View.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Adapter/ConfigBarAdapter.h"
#include "Display/Scrollable.h"
#include "Display/ScrollableWidget.h"
#include "RemoteDevelopmentService/LoggerHelper.h"

enum Settings {
    brightness = 0,
    enableBuzzer = 1,
    enableWifi = 2,
    ipAddress = 3,
    reboot = 4,
    goBack = 5,
};

class ConfigView final : public View {
    PreferencesManager &preferencesManager;
    std::function<void(DeviceModeState)> onDeviceModeChange;

    const std::vector<String> optionsList = {
        "Brightness",
        "Buzzer",
        "WiFi",
        preferencesManager.wifiIpAddress,
        " [Reboot]",
        " [Return]",
    };

    Scrollable scrollable;
    ScrollableWidget scrollableWidget;

public:
    explicit ConfigView(
        PreferencesManager &preferencesManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange
    )
        : preferencesManager(preferencesManager), onDeviceModeChange(onDeviceModeChange), scrollable(optionsList), scrollableWidget(scrollable) {
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

        onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonA.takeActionIfPossible()) {
            scrollable.cycleSelectedOption(-1);
            queueRender();
        }

        if (remoteInputManager.buttonB.takeActionIfPossible()) {
            scrollable.cycleSelectedOption(1);
            queueRender();
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            switch (scrollable.getSelectedOptionId()) {
                case Settings::brightness:
                    preferencesManager.settings.brightness =
                            (clamp(preferencesManager.settings.brightness / 32, 1, 8) - 1) * 32 + 31;
                    break;
                case Settings::enableWifi:
                    preferencesManager.settings.enableWifi = !preferencesManager.settings.enableWifi;
                    break;
                case Settings::enableBuzzer:
                    preferencesManager.settings.enableBuzzer = !preferencesManager.settings.enableBuzzer;
                    break;
                default:
                    break;
            }

            queueRender();
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            switch (scrollable.getSelectedOptionId()) {
                case Settings::brightness:
                    preferencesManager.settings.brightness =
                            clamp(preferencesManager.settings.brightness / 32 + 1, 0, 7) * 32 + 31;
                    break;
                case Settings::enableWifi:
                    preferencesManager.settings.enableWifi = !preferencesManager.settings.enableWifi;
                    break;
                case Settings::enableBuzzer:
                    preferencesManager.settings.enableBuzzer = !preferencesManager.settings.enableBuzzer;
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

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.resetHistoryBar();
        ledDisplay.setColonAppearance();
        ledDisplay.setGlyphsGlyph(Glyph::Empty, Glyph::Empty, Glyph::Empty, Glyph::Empty);
        ledDisplay.setPlayersIndicatorsState(true);
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        uint8_t value = 0;
        Color color;

        switch (scrollable.getSelectedOptionId()) {
            case Settings::brightness:
                color = Colors::White;
                ledDisplay.setGlyphsGlyph(Glyph::b, Glyph::r, Glyph::Empty,
                    LedDisplay::digitToGlyph(preferencesManager.settings.brightness / 32 + 1));
                break;
            case Settings::enableBuzzer:
                value = preferencesManager.settings.enableBuzzer;
                color = value ? Colors::Green : Colors::Red;
                ledDisplay.setGlyphsGlyph(Glyph::b, Glyph::U, Glyph::D2, Glyph::D2);
                break;
            case Settings::enableWifi:
                value = preferencesManager.settings.enableWifi;
                color = value ? Colors::Green : Colors::Red;
                ledDisplay.setGlyphsGlyph(Glyph::n, Glyph::E, Glyph::t, Glyph::Empty);
                break;
            case Settings::ipAddress:
                color = Colors::White;
                ledDisplay.setGlyphsGlyph(Glyph::D1, Glyph::P, Glyph::Empty, Glyph::Empty);
                break;
            case Settings::reboot:
                color = Colors::Red;
                ledDisplay.setGlyphsGlyph(Glyph::b, Glyph::D0, Glyph::D0, Glyph::t);
                break;
            default:
            case Settings::goBack:
                color = Colors::Blue;
                ledDisplay.setGlyphsGlyph(Glyph::r, Glyph::E, Glyph::t, Glyph::U);
                break;
        }

        ledDisplay.setGlyphsColor(color, color);
        ledDisplay.setBrightness(preferencesManager.settings.brightness);
        ledDisplay.setIndicatorAppearancePlayerA(color);
        ledDisplay.setIndicatorAppearancePlayerB(color);
        ledDisplay.setLedBarState(ConfigBarAdapter::toLedBarPixels(
            scrollable.getSelectedOptionId(),
            preferencesManager.settings.enableBuzzer,
            preferencesManager.settings.enableWifi
        ));
        ledDisplay.display();
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        scrollableWidget.render(backDisplay);
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //CONFIGVIEW_H
