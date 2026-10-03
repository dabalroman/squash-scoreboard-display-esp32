#ifndef GARMIN_VIEW_H
#define GARMIN_VIEW_H

#include "BatteryMonitor.h"
#include "Strings.h"
#include "DeviceMode/View.h"
#include "DeviceMode/ConfigMode/ConfigModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/Scrollable.h"
#include "Display/ScrollableWidget.h"
#include "Garmin/GarminService.h"

/**
 * CONFIG's GARMIN sub-screen: the enable toggle (live, no reboot), pairing, forgetting
 * every watch, and the way back. Paired / connected counts sit in the e-paper footer
 * (on V1's OLED, the paired count on the top strip; the badge carries the connected one).
 */
class GarminView final : public View {
    enum Row : uint8_t {
        enabled = 0,
        pair = 1,
        forget = 2,
        goBack = 3,
    };

    GarminService &garminService;
    const BatteryMonitor &batteryMonitor;
    std::function<void(ConfigModeState)> onStateChange;

    // Not const: the forget row swaps to its confirmation label in place. Scrollable binds
    // it by reference and snapshots its size, which never changes.
    std::vector<String> optionsList = {
        Str::GARMIN_OPTION_ENABLED_OLED,
        Str::GARMIN_OPTION_PAIR_OLED,
        Str::GARMIN_OPTION_FORGET_OLED,
        Str::CONFIG_OPTION_RETURN_OLED,
    };

    Scrollable scrollable;
    ScrollableWidget scrollableWidget;

    // Forgetting is irreversible: the first D arms it, a second D on the same row does it.
    bool forgetArmed = false;
    uint8_t shownPairedCount = 0;

    void setForgetArmed(const bool armed) {
        forgetArmed = armed;
        optionsList[forget] = armed ? Str::GARMIN_OPTION_FORGET_CONFIRM_OLED : Str::GARMIN_OPTION_FORGET_OLED;
    }

public:
    GarminView(
        GarminService &garminService,
        const BatteryMonitor &batteryMonitor,
        const std::function<void(ConfigModeState)> &onStateChange,
        const uint8_t selectedOption = 0
    )
        : garminService(garminService), batteryMonitor(batteryMonitor), onStateChange(onStateChange),
          scrollable(optionsList), scrollableWidget(scrollable) {
        scrollable.setSelectedOption(selectedOption);
        shownPairedCount = garminService.pairedCount();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonA.takeActionIfPossible()) {
            setForgetArmed(false);
            scrollable.cycleSelectedOption(-1);
            queueRender();
        }

        if (remoteInputManager.buttonB.takeActionIfPossible()) {
            setForgetArmed(false);
            scrollable.cycleSelectedOption(1);
            queueRender();
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            if (scrollable.getSelectedOptionId() == Row::enabled) {
                garminService.setEnabled(!garminService.isEnabled());
            }
            setForgetArmed(false);
            queueRender();
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            switch (scrollable.getSelectedOptionId()) {
                case Row::enabled:
                    garminService.setEnabled(!garminService.isEnabled());
                    break;
                case Row::pair:
                    // Nothing to pair with until NimBLE is up; the row's red says so.
                    if (garminService.running()) {
                        onStateChange(ConfigModeState::GarminPairing);
                    }
                    break;
                case Row::forget:
                    if (forgetArmed) {
                        garminService.forgetAll();
                        setForgetArmed(false);
                    } else {
                        setForgetArmed(true);
                    }
                    break;
                case Row::goBack:
                default:
                    onStateChange(ConfigModeState::Menu);
                    break;
            }

            queueRender();
        }

        // A pairing elsewhere or a forget lands here; the OLED is event-driven.
        const uint8_t paired = garminService.pairedCount();
        if (paired != shownPairedCount) {
            shownPairedCount = paired;
            shouldRenderBack = true;
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.resetAnimations();
        ledDisplay.setColonAppearance();
        ledDisplay.setPlayersIndicatorsState(true);
        ledDisplay.setBorderEnabled(false);
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        Color color;
        bool blink = false;

        switch (scrollable.getSelectedOptionId()) {
            case Row::enabled:
                color = garminService.isEnabled() ? Colors::Green : Colors::Red;
                ledDisplay.setGlyphsText(garminService.isEnabled() ? Str::LED_GARMIN_ON : Str::LED_GARMIN_OFF);
                break;
            case Row::pair:
                color = garminService.running() ? Colors::Blue : Colors::Red;
                ledDisplay.setGlyphsText(Str::LED_GARMIN_PAIR);
                break;
            case Row::forget:
                color = forgetArmed ? Colors::Red : Colors::Orange;
                blink = forgetArmed;
                ledDisplay.setGlyphsText(Str::LED_GARMIN_FORGET);
                break;
            case Row::goBack:
            default:
                color = Colors::Aqua;
                ledDisplay.setGlyphsText(Str::LED_CONFIG_RETURN);
                break;
        }

        ledDisplay.setGlyphsColor(color, color);
        ledDisplay.setGlyphBlinking(blink, blink);
        ledDisplay.setIndicatorAppearancePlayerA(color);
        ledDisplay.setIndicatorAppearancePlayerB(color);
        ledDisplay.display();
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available()) {
            return;
        }

        const EInkMenuRow rows[] = {
            {Str::GARMIN_ROW_ENABLED_LABEL, nullptr, static_cast<int8_t>(garminService.isEnabled() ? 1 : 0)},
            {Str::GARMIN_ROW_PAIR_LABEL, nullptr, -1},
            {forgetArmed ? Str::GARMIN_ROW_FORGET_CONFIRM_LABEL : Str::GARMIN_ROW_FORGET_LABEL, nullptr, -1},
            {Str::CONFIG_ROW_RETURN_LABEL, nullptr, -1},
        };

        char paired[20];
        snprintf(paired, sizeof(paired), Str::GARMIN_PAIRED_FMT,
                 static_cast<unsigned>(garminService.pairedCount()));
        char connected[20];
        snprintf(connected, sizeof(connected), Str::GARMIN_CONNECTED_FMT,
                 static_cast<unsigned>(garminService.running() ? garminService.authedCount() : 0));


        einkDisplay.showMenu(Str::GARMIN_MENU_TITLE, rows, sizeof(rows) / sizeof(rows[0]),
                             scrollable.getSelectedOptionId(),
                             EInkFooter(nullptr, paired, connected, -1));
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        scrollableWidget.render(backDisplay);
        // V1 has no e-paper footer; its lit top strip takes the paired count instead.
        if (!EInkDisplay::available()) {
            char paired[20];
            snprintf(paired, sizeof(paired), Str::GARMIN_PAIRED_FMT, static_cast<unsigned>(shownPairedCount));
            backDisplay.drawTopLeftNote(paired);
        }
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //GARMIN_VIEW_H
