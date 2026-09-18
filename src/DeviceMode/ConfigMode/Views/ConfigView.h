#ifndef CONFIGVIEW_H
#define CONFIGVIEW_H

#include <version.h>

#include "BatteryMonitor.h"
#include "Strings.h"
#include "SafeRestart.h"
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
    reboot = 3,
    goBack = 4,
};

class ConfigView final : public View {
    PreferencesManager &preferencesManager;
    std::function<void(DeviceModeState)> onDeviceModeChange;
    const BatteryMonitor &batteryMonitor;
    int16_t shownBatteryPercent = -1;

    const std::vector<String> optionsList = {
        Str::CONFIG_OPTION_BRIGHTNESS_OLED,
        Str::CONFIG_OPTION_BUZZER_OLED,
        Str::CONFIG_OPTION_WIFI_OLED,
        Str::CONFIG_OPTION_REBOOT_OLED,
        Str::CONFIG_OPTION_RETURN_OLED,
    };

    Scrollable scrollable;
    ScrollableWidget scrollableWidget;

public:
    explicit ConfigView(
        PreferencesManager &preferencesManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        const BatteryMonitor &batteryMonitor
    )
        : preferencesManager(preferencesManager), onDeviceModeChange(onDeviceModeChange),
          batteryMonitor(batteryMonitor), scrollable(optionsList), scrollableWidget(scrollable) {
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
            safeRestart();
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
        ledDisplay.resetAnimations();
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
                {
                    // The word carries the label; the last position is the level.
                    LedWord word = LedText::toWord(Str::LED_CONFIG_BRIGHTNESS);
                    word.d = LedDisplay::digitToGlyph(preferencesManager.settings.brightness / 32 + 1);
                    ledDisplay.setGlyphsGlyph(word);
                }
                break;
            case Settings::enableBuzzer:
                value = preferencesManager.settings.enableBuzzer;
                color = value ? Colors::Green : Colors::Red;
                ledDisplay.setGlyphsText(Str::LED_CONFIG_BUZZER);
                break;
            case Settings::enableWifi:
                value = preferencesManager.settings.enableWifi;
                color = value ? Colors::Green : Colors::Red;
                ledDisplay.setGlyphsText(Str::LED_CONFIG_WIFI);
                break;
            case Settings::reboot:
                color = Colors::Pink;
                ledDisplay.setGlyphsText(Str::LED_CONFIG_REBOOT);
                break;
            default:
            case Settings::goBack:
                color = Colors::Aqua;
                ledDisplay.setGlyphsText(Str::LED_CONFIG_RETURN);
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

        // TODO: USE SCROLLABLE WIDGET
        const PrefsData &settings = preferencesManager.settings;

        char brightnessLevel[4];
        snprintf(brightnessLevel, sizeof(brightnessLevel), "%u/8", settings.brightness / 32 + 1);

        // Index-aligned with `Settings` / optionsList. The two toggles show a
        // tickbox here; the rear OLED keeps its TAK/NIE value column.
        const EInkMenuRow rows[] = {
            {Str::CONFIG_ROW_BRIGHTNESS_LABEL, brightnessLevel, -1},
            {Str::CONFIG_ROW_BUZZER_LABEL, nullptr, static_cast<int8_t>(settings.enableBuzzer ? 1 : 0)},
            {Str::CONFIG_ROW_WIFI_LABEL, nullptr, static_cast<int8_t>(settings.enableWifi ? 1 : 0)},
            {Str::CONFIG_ROW_REBOOT_LABEL, nullptr, -1},
            {Str::CONFIG_ROW_RETURN_LABEL, nullptr, -1},
        };

        const int16_t batteryPercent = batteryMonitor.available()
                                           ? static_cast<int16_t>(batteryMonitor.percent())
                                           : -1;

        // Footer lines, top to bottom: battery, firmware version, IP. The version is
        // the only place it is shown on the device - the splash is an image now.
        // The IP is a line rather than a row because it is never actionable, and as
        // a row it both cost a scroll stop and was the one label too wide to fit.
        const String &ip = preferencesManager.wifiIpAddress;

        char version[16];
        snprintf(version, sizeof(version), "V%s", FW_VERSION);

        einkDisplay.showMenu(Str::CONFIG_MENU_TITLE, rows, sizeof(rows) / sizeof(rows[0]),
                             scrollable.getSelectedOptionId(),
                             EInkFooter(nullptr, version,
                                        ip.length() > 0 ? ip.c_str() : nullptr, batteryPercent));
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        const int16_t batteryPercent = batteryMonitor.available()
            ? static_cast<int16_t>(batteryMonitor.percent())
            : -1;

        // Refresh while open whenever the shown battery value changes.
        if (batteryPercent != shownBatteryPercent) {
            shouldRenderBack = true;
        }

        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        scrollableWidget.render(backDisplay);

        if (batteryPercent >= 0) {
            renderBattery(backDisplay, batteryPercent);
        }

        shownBatteryPercent = batteryPercent;
        backDisplay.display();

        shouldRenderBack = false;
    }

private:
    static void renderBattery(BackDisplay &backDisplay, const int16_t percent) {
        char text[10];
        snprintf(text, sizeof(text), "BAT %d%%", percent);

        backDisplay.printStatusRight(text);
    }
};

#endif //CONFIGVIEW_H
