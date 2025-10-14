#ifndef CONFIGVIEW_H
#define CONFIGVIEW_H

#include "DeviceMode/View.h"
#include "Display/GlyphDisplay.h"
#include "Display/Scrollable.h"
#include "Display/ScrollableWidget.h"
#include "RemoteDevelopmentService/LoggerHelper.h"

enum Settings {
    brightness = 0,
    enableWifi = 1,
    enableAP = 2,
    ipAddress = 3,
    reboot = 4,
    goBack = 5,
};

class ConfigView final : public View {
    PreferencesManager &preferencesManager;
    std::function<void(DeviceModeState)> onDeviceModeChange;

    const std::vector<String> optionsList = {
        "Brightness",
        "WiFi",
        "Fallbck AP",
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
                case Settings::enableAP:
                    preferencesManager.settings.enableAp = !preferencesManager.settings.enableAp;
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

        switch (scrollable.getSelectedOptionId()) {
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
        glyphDisplay.setGlyphsGlyph(Glyph::Empty, Glyph::Empty, Glyph::Empty, Glyph::Empty);
        glyphDisplay.setIndicatorAppearancePlayerA(color);
        glyphDisplay.setIndicatorAppearancePlayerB(color);
        glyphDisplay.display();

        shouldRenderGlyphs = false;
    }

    void renderScreen(BackDisplay &backDisplay) override {
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
