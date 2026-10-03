#ifndef CONFIGVIEW_H
#define CONFIGVIEW_H

#include <version.h>

#include "BatteryMonitor.h"
#include "Strings.h"
#include "SafeRestart.h"
#include "DeviceMode/View.h"
#include "DeviceMode/ConfigMode/ConfigModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Renderer/ConfigBarRenderer.h"
#include "Display/Scrollable.h"
#include "Display/ScrollableWidget.h"
#include "RemoteDevelopmentService/LoggerHelper.h"

enum Settings {
    brightness = 0,
    buzzer = 1,
    devMode = 2,
    garmin = 3,
    reboot = 4,
    goBack = 5,
};

class ConfigView final : public View {
    PreferencesManager &preferencesManager;
    std::function<void(DeviceModeState)> onDeviceModeChange;
    const BatteryMonitor &batteryMonitor;
    std::function<void(ConfigModeState)> onStateChange;

    const std::vector<String> optionsList = {
        Str::CONFIG_OPTION_BRIGHTNESS_OLED,
        Str::CONFIG_OPTION_BUZZER_OLED,
        Str::CONFIG_OPTION_DEV_MODE_OLED,
        Str::CONFIG_OPTION_GARMIN_OLED,
        Str::CONFIG_OPTION_REBOOT_OLED,
        Str::CONFIG_OPTION_RETURN_OLED,
    };

    Scrollable scrollable;
    ScrollableWidget scrollableWidget;

public:
    explicit ConfigView(
        PreferencesManager &preferencesManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        const BatteryMonitor &batteryMonitor,
        const std::function<void(ConfigModeState)> &onStateChange,
        const uint8_t selectedOption = 0
    )
        : preferencesManager(preferencesManager), onDeviceModeChange(onDeviceModeChange),
          batteryMonitor(batteryMonitor), onStateChange(onStateChange),
          scrollable(optionsList), scrollableWidget(scrollable) {
        scrollable.setSelectedOption(selectedOption);
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
                case Settings::devMode:
                    preferencesManager.settings.enableDevMode = !preferencesManager.settings.enableDevMode;
                    break;
                case Settings::buzzer:
                    preferencesManager.settings.buzzerMode = PrefsBuzzer::prev(preferencesManager.settings.buzzerMode);
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
                case Settings::devMode:
                    preferencesManager.settings.enableDevMode = !preferencesManager.settings.enableDevMode;
                    break;
                case Settings::buzzer:
                    preferencesManager.settings.buzzerMode = PrefsBuzzer::next(preferencesManager.settings.buzzerMode);
                    break;
                case Settings::garmin:
                    // Opens the sub-screen; the flag lives in the "gar" blob, never PrefsData.
                    onStateChange(ConfigModeState::Garmin);
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
        // GarminView blinks the forget confirmation; coming back must not carry it over.
        ledDisplay.setGlyphBlinking(false, false);
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
            case Settings::buzzer:
                color = ConfigBarRenderer::buzzerColor(preferencesManager.settings.buzzerMode);
                ledDisplay.setGlyphsText(Str::LED_CONFIG_BUZZER);
                break;
            case Settings::devMode:
                value = preferencesManager.settings.enableDevMode;
                color = value ? Colors::Green : Colors::Red;
                // No glyph exists for D-E-V or M-O-D-E; the colour carries the state.
                ledDisplay.setGlyphsGlyph(Glyph::Empty, Glyph::Empty, Glyph::Empty, Glyph::Empty);
                break;
            case Settings::garmin:
                // A sub-menu entry, not a switch: its on/off lives inside (AKTYWNY).
                color = Colors::Blue;
                ledDisplay.setGlyphsText(Str::LED_CONFIG_GARMIN);
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
            preferencesManager.settings.buzzerMode,
            preferencesManager.settings.enableDevMode
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
        // tickbox here (the buzzer's third state is an "M" in it); the rear OLED lists
        // labels only.
        const EInkMenuRow rows[] = {
            {Str::CONFIG_ROW_BRIGHTNESS_LABEL, brightnessLevel, -1},
            // check 0 / 1 / 2 is the stored byte itself.
            {Str::CONFIG_ROW_BUZZER_LABEL, nullptr, static_cast<int8_t>(PrefsBuzzer::normalize(settings.buzzerMode))},
            {Str::CONFIG_ROW_DEV_MODE_LABEL, nullptr, static_cast<int8_t>(settings.enableDevMode ? 1 : 0)},
            {Str::CONFIG_ROW_GARMIN_LABEL, nullptr, -1},
            {Str::CONFIG_ROW_REBOOT_LABEL, nullptr, -1},
            {Str::CONFIG_ROW_RETURN_LABEL, nullptr, -1},
        };


        // Footer lines: firmware version, IP (the battery is the main screen's only). The version is
        // the only place it is shown on the device - the splash is an image now.
        // The IP is a line rather than a row because it is never actionable, and as
        // a row it both cost a scroll stop and was the one label too wide to fit.
        const String &ip = preferencesManager.wifiIpAddress;

        char version[16];
        snprintf(version, sizeof(version), "V%s", FW_VERSION);

        einkDisplay.showMenu(Str::CONFIG_MENU_TITLE, rows, sizeof(rows) / sizeof(rows[0]),
                             scrollable.getSelectedOptionId(),
                             EInkFooter(nullptr, version,
                                        ip.length() > 0 ? ip.c_str() : nullptr, -1));
    }

    // The battery reaches the OLED only where there is no e-paper to carry it (V1).
    // Gated on the two facts, never on the board.
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
