#ifndef TOURNAMENT_WAIT_VIEW_H
#define TOURNAMENT_WAIT_VIEW_H

#include <functional>

#include "Strings.h"
#include "Color.h"
#include "DeviceMode/DeviceModeState.h"
#include "DeviceMode/View.h"
#include "Display/LedDisplay/LedDisplay.h"

/**
 * TOURNAMENT_WAIT (spec 20): nothing to do on the board but wait for a tablet's BEGIN, or
 * leave with C. Event-driven: every display draws once per queueRender().
 */
class TournamentWaitView final : public View {
    std::function<void(DeviceModeState)> onDeviceModeChange;

public:
    explicit TournamentWaitView(const std::function<void(DeviceModeState)> &onDeviceModeChange)
        : onDeviceModeChange(onDeviceModeChange) {
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            exitToMenu();
        }
    }

    void exitToMenu() {
        onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.resetAnimations();
        ledDisplay.setColonAppearance();
        ledDisplay.setPlayersIndicatorsState(true);
        ledDisplay.setBorderEnabled(true);
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        if (!shouldRenderLedDisplay) {
            return;
        }

        ledDisplay.setGlyphsText(Str::LED_MODE_TOURNAMENT);
        ledDisplay.setGlyphsColor(Colors::Pink, Colors::Pink);
        ledDisplay.setIndicatorAppearancePlayerA(Colors::Pink);
        ledDisplay.setIndicatorAppearancePlayerB(Colors::Pink);
        ledDisplay.setBorderAppearance(Colors::Pink, Colors::Pink, false, false);
        ledDisplay.display();

        shouldRenderLedDisplay = false;
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.initSmallFont();
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        backDisplay.setCursorToLine(0, 0);
        backDisplay.print(Str::TOURNAMENT_WAIT_TITLE);
        backDisplay.setCursorToLine(0, 1);
        backDisplay.print(Str::TOURNAMENT_WAIT_LINE1);
        backDisplay.setCursorToLine(0, 2);
        backDisplay.print(Str::TOURNAMENT_WAIT_LINE2);
        backDisplay.display();

        shouldRenderBack = false;
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available()) {
            return;
        }

        einkDisplay.showCode(Str::TOURNAMENT_WAIT_TITLE, Str::TOURNAMENT_WAIT_CODE, Str::TOURNAMENT_WAIT_LINE1,
                             Str::TOURNAMENT_WAIT_LINE2);
    }

    // Busy for a watch; main.cpp publishes the screen from WatchSport's table.
    void describeForWatch(WatchState &state) const override {
        state.setBusy(Garmin::ScreenId::TournamentWait);
    }
};

#endif //TOURNAMENT_WAIT_VIEW_H
