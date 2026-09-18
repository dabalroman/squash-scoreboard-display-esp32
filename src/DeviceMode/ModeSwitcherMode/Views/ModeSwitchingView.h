#ifndef MODE_SWITCHING_VIEW_H
#define MODE_SWITCHING_VIEW_H

#include "BatteryMonitor.h"
#include "Strings.h"
#include "DeviceMode/DeviceModeState.h"
#include "DeviceMode/View.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Renderer/ModeSwitchingBarRenderer.h"
#include "Display/Scrollable.h"
#include "Display/ScrollableWidget.h"

enum Options {
    Squash = 0,
    Volleyball = 1,
    ShortVolleyball = 2,
    Padel = 3,
    Config = 4,
};

class ModeSwitchingView final : public View {
    std::function<void(DeviceModeState)> onDeviceModeChange;
    const BatteryMonitor &batteryMonitor;
    int16_t shownBatteryPercent = -1;

    const std::vector<String> optionsList = {
        Str::MODE_OPTION_SQUASH_OLED,
        Str::MODE_OPTION_VOLLEYBALL_OLED,
        Str::MODE_OPTION_SHORT_VOLLEYBALL_OLED,
        Str::MODE_OPTION_PADEL_OLED,
        Str::MODE_OPTION_CONFIG_OLED,
    };

    Scrollable scrollable;
    ScrollableWidget scrollableWidget;

public:
    explicit ModeSwitchingView(
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        const BatteryMonitor &batteryMonitor
    )
        : onDeviceModeChange(onDeviceModeChange), batteryMonitor(batteryMonitor),
          scrollable(optionsList), scrollableWidget(scrollable) {
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

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            switch (scrollable.getSelectedOptionId()) {
                case Options::Squash:
                    onDeviceModeChange(DeviceModeState::SquashMode);
                    break;
                case Options::Volleyball:
                    onDeviceModeChange(DeviceModeState::VolleyballMode);
                    break;
                case Options::ShortVolleyball:
                    onDeviceModeChange(DeviceModeState::ShortVolleyballMode);
                    break;
                case Options::Padel:
                    onDeviceModeChange(DeviceModeState::PadelMode);
                    break;
                case Options::Config:
                    onDeviceModeChange(DeviceModeState::ConfigMode);
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
        ledDisplay.setPlayersIndicatorsState(true);
        ledDisplay.setBorderEnabled(false);
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        if (!shouldRenderLedDisplay) {
            return;
        }

        Color color;

        switch (scrollable.getSelectedOptionId()) {
            default:
            case Options::Squash:
                color = Colors::Green;
                ledDisplay.setGlyphsText(Str::LED_MODE_SQUASH);
                break;
            case Options::Volleyball:
                color = Colors::Yellow;
                ledDisplay.setGlyphsText(Str::LED_MODE_VOLLEYBALL);
                break;
            case Options::ShortVolleyball:
                color = Colors::Orange;
                ledDisplay.setGlyphsText(Str::LED_MODE_SHORT_VOLLEYBALL);
                break;
            case Options::Padel:
                color = Colors::Blue;
                ledDisplay.setGlyphsText(Str::LED_MODE_PADEL);
                break;
            case Options::Config:
                color = Colors::White;
                ledDisplay.setGlyphsText(Str::LED_MODE_CONFIG);
                break;
        }

        ledDisplay.setGlyphsColor(color, color);
        ledDisplay.setIndicatorAppearancePlayerA(color);
        ledDisplay.setIndicatorAppearancePlayerB(color);
        ledDisplay.setLedBarState([&] { return ModeSwitchingBarRenderer::toLedBarPixels(scrollable.getSelectedOptionId()); });
        ledDisplay.display();

        shouldRenderLedDisplay = false;
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available()) {
            return;
        }

        // E-paper labels, index-aligned with `Options` / optionsList.
        static const char *const labels[] = {
            Str::MODE_OPTION_SQUASH, Str::MODE_OPTION_VOLLEYBALL, Str::MODE_OPTION_SHORT_VOLLEYBALL,
            Str::MODE_OPTION_PADEL, Str::MODE_OPTION_CONFIG,
        };
        constexpr uint8_t count = sizeof(labels) / sizeof(labels[0]);

        EInkMenuRow rows[count];
        for (uint8_t i = 0; i < count; i++) {
            rows[i] = {labels[i], nullptr, -1};
        }

        // The battery percent rides in the title; without a sensor the title is plain.
        char title[16];
        snprintf(title, sizeof(title), "%s", Str::MODE_MENU_TITLE);
        if (batteryMonitor.available()) {
            snprintf(title, sizeof(title), Str::MODE_MENU_TITLE_BATTERY_FMT, batteryMonitor.percent());
        }

        einkDisplay.showMenu(title, rows, count, scrollable.getSelectedOptionId());
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        const int16_t batteryPercent = batteryMonitor.available()
            ? static_cast<int16_t>(batteryMonitor.percent())
            : -1;

        // Redraw while the menu is open whenever the shown percent changes.
        if (batteryPercent != shownBatteryPercent) {
            shouldRenderBack = true;
        }

        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        scrollableWidget.render(backDisplay);

        if (batteryPercent >= 0) {
            char text[10];
            snprintf(text, sizeof(text), "BAT %d%%", batteryPercent);
            backDisplay.printStatusRight(text);
        }

        shownBatteryPercent = batteryPercent;
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //MODE_SWITCHING_VIEW_H
