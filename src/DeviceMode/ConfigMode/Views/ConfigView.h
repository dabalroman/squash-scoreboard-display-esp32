#ifndef CONFIGVIEW_H
#define CONFIGVIEW_H

#include <version.h>

#include "BatterySensor.h"
#include "DeviceMode/View.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Renderer/ConfigBarRenderer.h"
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
    const BatterySensor &batterySensor;
    int32_t shownBatteryCentivolts = -1;

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
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        const BatterySensor &batterySensor
    )
        : preferencesManager(preferencesManager), onDeviceModeChange(onDeviceModeChange),
          batterySensor(batterySensor), scrollable(optionsList), scrollableWidget(scrollable) {
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
        ledDisplay.setBorderEnabled(false);
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
                ledDisplay.setGlyphsGlyph(Glyph::b, Glyph::u, Glyph::Z, Glyph::Z);
                break;
            case Settings::enableWifi:
                value = preferencesManager.settings.enableWifi;
                color = value ? Colors::Green : Colors::Red;
                ledDisplay.setGlyphsGlyph(Glyph::c, Glyph::o, Glyph::n, Glyph::n);
                break;
            case Settings::ipAddress:
                color = Colors::White;
                ledDisplay.setGlyphsGlyph(Glyph::I, Glyph::P, Glyph::Empty, Glyph::Empty);
                break;
            case Settings::reboot:
                color = Colors::Pink;
                ledDisplay.setGlyphsGlyph(Glyph::b, Glyph::o, Glyph::o, Glyph::t);
                break;
            default:
            case Settings::goBack:
                color = Colors::Aqua;
                ledDisplay.setGlyphsGlyph(Glyph::r, Glyph::E, Glyph::t, Glyph::u);
                break;
        }

        ledDisplay.setGlyphsColor(color, color);
        ledDisplay.setBrightness(preferencesManager.settings.brightness);
        ledDisplay.setIndicatorAppearancePlayerA(color);
        ledDisplay.setIndicatorAppearancePlayerB(color);
        ledDisplay.setLedBarState([&] { return ConfigBarRenderer::toLedBarPixels(
            scrollable.getSelectedOptionId(),
            preferencesManager.settings.enableBuzzer,
            preferencesManager.settings.enableWifi
        ); });
        ledDisplay.display();
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available()) {
            return;
        }

        const PrefsData &settings = preferencesManager.settings;

        char brightnessLevel[4];
        snprintf(brightnessLevel, sizeof(brightnessLevel), "%u/8", settings.brightness / 32 + 1);

        // Index-aligned with `Settings` / optionsList.
        const EInkMenuRow rows[] = {
            {"Bright", brightnessLevel, -1},
            {"Buzzer", settings.enableBuzzer ? "ON" : "OFF", -1},
            {"WiFi", settings.enableWifi ? "ON" : "OFF", -1},
            {preferencesManager.wifiIpAddress.c_str(), nullptr, -1},
            {"Reboot", nullptr, -1},
            {"Return", nullptr, -1},
        };

        char footer[16] = "";
        if (batterySensor.available()) {
            const int32_t centivolts = static_cast<int32_t>(lround(batterySensor.volts() * 100.0f));
            snprintf(footer, sizeof(footer), "BAT %d.%02dV",
                     static_cast<int>(centivolts / 100), static_cast<int>(centivolts % 100));
        }

        // The only place the firmware version is shown on the device - the splash is
        // an image now. A footer line, not a row, keeps the option indices untouched.
        char version[24];
        snprintf(version, sizeof(version), "FW %s", FW_VERSION);

        // Without a battery reading the version takes the single footer line instead.
        const bool hasBattery = footer[0] != '\0';

        einkDisplay.showMenu("CONFIG", rows, sizeof(rows) / sizeof(rows[0]),
                             scrollable.getSelectedOptionId(),
                             hasBattery ? footer : version, hasBattery ? version : nullptr);
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        const int32_t batteryCentivolts = batterySensor.available()
            ? static_cast<int32_t>(lround(batterySensor.volts() * 100.0f))
            : -1;

        // Refresh while open whenever the shown battery value changes.
        if (batteryCentivolts != shownBatteryCentivolts) {
            shouldRenderBack = true;
        }

        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        scrollableWidget.render(backDisplay);

        if (batteryCentivolts >= 0) {
            renderBattery(backDisplay, batteryCentivolts);
        }

        shownBatteryCentivolts = batteryCentivolts;
        backDisplay.display();

        shouldRenderBack = false;
    }

private:
    // Built-in 6x8 font in the free strip above the first 9pt line (rows 0-6),
    // right-aligned, so the menu layout does not move.
    static void renderBattery(BackDisplay &backDisplay, const int32_t centivolts) {
        char text[12];
        snprintf(text, sizeof(text), "BAT %d.%02dV",
                 static_cast<int>(centivolts / 100), static_cast<int>(centivolts % 100));

        backDisplay.screen->setFont(nullptr);
        backDisplay.screen->setCursor(128 - static_cast<int16_t>(strlen(text)) * 6, 0);
        backDisplay.screen->print(text);
        backDisplay.initSmallFont();
    }
};

#endif //CONFIGVIEW_H
