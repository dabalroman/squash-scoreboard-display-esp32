#ifndef MODE_SWITCHING_VIEW_H
#define MODE_SWITCHING_VIEW_H

#include <vector>

#include "Board.h"
#include "BatteryMonitor.h"
#include "Strings.h"
#include "DeviceMode/DeviceModeState.h"
#include "DeviceMode/View.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Renderer/ModeSwitchingBarRenderer.h"
#include "Display/Scrollable.h"
#include "Display/ScrollableWidget.h"

/**
 * One row of the mode selector. Everything a row needs sits in one struct, so the
 * OLED label, the e-paper label, the LED word, the colour and the mode it opens
 * cannot drift out of alignment - which is what a hand-kept enum plus three
 * parallel lists used to allow.
 *
 * `enabled` is a runtime flag, never an `#if`: BOARD_REV belongs in Board.h and
 * the hardware wrappers only.
 */
struct ModeMenuEntry {
    const char *optionOled;      // space-padded to 10, for the OLED fixed-x centring
    const char *einkLabel;
    const char *ledWord;
    DeviceModeState target;
    Color color;
    int8_t barSlot;              // V1 history bar segment; -1 = no segment
    bool enabled;
};

class ModeSwitchingView final : public View {
    std::function<void(DeviceModeState)> onDeviceModeChange;
    const BatteryMonitor &batteryMonitor;

    // Declaration order is the correctness argument: entryIds feeds optionsList,
    // which Scrollable binds by reference and whose size it snapshots. Both are
    // const and never resized after construction, so that reference and that
    // count stay valid for the life of the view.
    const std::vector<uint8_t> entryIds;
    const std::vector<String> optionsList;

    Scrollable scrollable;
    ScrollableWidget scrollableWidget;

public:
    explicit ModeSwitchingView(
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        const BatteryMonitor &batteryMonitor
    )
        : onDeviceModeChange(onDeviceModeChange), batteryMonitor(batteryMonitor),
          entryIds(buildEntryIds()), optionsList(buildOptions(entryIds)),
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
            // The callback destroys this view, so nothing may touch `this` after it.
            onDeviceModeChange(selectedEntry().target);
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

        const ModeMenuEntry &entry = selectedEntry();

        ledDisplay.setGlyphsText(entry.ledWord);
        ledDisplay.setGlyphsColor(entry.color, entry.color);
        ledDisplay.setIndicatorAppearancePlayerA(entry.color);
        ledDisplay.setIndicatorAppearancePlayerB(entry.color);
        ledDisplay.setLedBarState([&] {
            return ModeSwitchingBarRenderer::toLedBarPixels(entry.barSlot, entry.color);
        });
        ledDisplay.display();

        shouldRenderLedDisplay = false;
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available()) {
            return;
        }

        // TODO: USE SCROLLABLE WIDGET
        EInkMenuRow rows[MAX_ENTRIES];
        const uint8_t count = static_cast<uint8_t>(entryIds.size());
        for (uint8_t i = 0; i < count; i++) {
            rows[i] = {entryAt(i).einkLabel, nullptr, -1};
        }

        // The battery sits in the footer with its icon; the title is just the title.
        const int16_t batteryPercent = batteryMonitor.available()
                                           ? static_cast<int16_t>(batteryMonitor.percent())
                                           : -1;

        einkDisplay.showMenu(Str::MODE_MENU_TITLE, rows, count, scrollable.getSelectedOptionId(),
                             EInkFooter(nullptr, nullptr, nullptr, batteryPercent));
    }

    // The battery is an e-paper-only readout now: on the OLED it lived in the top
    // strip the damaged panel never lights, and the 3-row menu below leaves it
    // nowhere else to go.
    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        scrollableWidget.render(backDisplay);
        backDisplay.display();

        shouldRenderBack = false;
    }

private:
    // Upper bound for the stack-allocated e-paper row array.
    enum : uint8_t { MAX_ENTRIES = 8 };

    // Function-local static, never a `static constexpr` class member: that is an
    // ODR link error on GCC 8.4.
    static const ModeMenuEntry *table(uint8_t &count) {
        static const ModeMenuEntry TABLE[] = {
            {Str::MODE_OPTION_PADEL_OLED, Str::MODE_OPTION_PADEL, Str::LED_MODE_PADEL,
             DeviceModeState::PadelMode, Colors::Blue, 3, true},
            {Str::MODE_OPTION_SQUASH_OLED, Str::MODE_OPTION_SQUASH, Str::LED_MODE_SQUASH,
             DeviceModeState::SquashMode, Colors::Green, 0, true},
            {Str::MODE_OPTION_VOLLEYBALL_OLED, Str::MODE_OPTION_VOLLEYBALL, Str::LED_MODE_VOLLEYBALL,
             DeviceModeState::VolleyballMode, Colors::Yellow, 1, true},
            {Str::MODE_OPTION_SHORT_VOLLEYBALL_OLED, Str::MODE_OPTION_SHORT_VOLLEYBALL,
             Str::LED_MODE_SHORT_VOLLEYBALL, DeviceModeState::ShortVolleyballMode, Colors::Orange, 2, true},
            {Str::MODE_OPTION_PLAYERS_OLED, Str::MODE_OPTION_PLAYERS, Str::LED_MODE_PLAYERS,
             DeviceModeState::PlayerSetupMode, Colors::Aqua, -1, Board::HAS_PLAYER_SETUP},
            {Str::MODE_OPTION_CONFIG_OLED, Str::MODE_OPTION_CONFIG, Str::LED_MODE_CONFIG,
             DeviceModeState::ConfigMode, Colors::White, -1, true},
        };

        count = static_cast<uint8_t>(sizeof(TABLE) / sizeof(TABLE[0]));
        return TABLE;
    }

    static std::vector<uint8_t> buildEntryIds() {
        uint8_t count = 0;
        const ModeMenuEntry *entries = table(count);

        std::vector<uint8_t> ids;
        ids.reserve(count);
        for (uint8_t i = 0; i < count; i++) {
            if (entries[i].enabled) {
                ids.push_back(i);
            }
        }

        return ids;
    }

    static std::vector<String> buildOptions(const std::vector<uint8_t> &ids) {
        uint8_t count = 0;
        const ModeMenuEntry *entries = table(count);

        std::vector<String> options;
        options.reserve(ids.size());
        for (std::vector<uint8_t>::const_iterator it = ids.begin(); it != ids.end(); ++it) {
            options.push_back(entries[*it].optionOled);
        }

        return options;
    }

    const ModeMenuEntry &entryAt(const uint8_t index) const {
        uint8_t count = 0;
        const ModeMenuEntry *entries = table(count);
        return entries[entryIds[index < entryIds.size() ? index : 0]];
    }

    const ModeMenuEntry &selectedEntry() const {
        return entryAt(scrollable.getSelectedOptionId());
    }
};

#endif //MODE_SWITCHING_VIEW_H
