#ifndef TOURNAMENT_WAIT_MODE_H
#define TOURNAMENT_WAIT_MODE_H

#include <functional>

#include "Utils.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/TabletSupport.h"
#include "Views/TournamentWaitView.h"

/**
 * TOURNAMENT on the main menu (spec 20): waits for a tablet's BEGIN_TOURNAMENT, then asks
 * main.cpp for the sport's mode in tablet mode (`onTabletBegin`). Only a person enters it.
 */
class TournamentWaitMode final : public DeviceMode {
    TabletSession &session;
    TabletLink &link;
    std::function<void(DeviceModeState)> onTabletBegin;
    TournamentWaitView *view = nullptr;

public:
    TournamentWaitMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        EInkDisplay &einkDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        TabletSession &session,
        TabletLink &link,
        std::function<void(DeviceModeState)> onTabletBegin
    )
        : DeviceMode(ledDisplay, backDisplay, einkDisplay, remoteInputManager, onDeviceModeChange),
          session(session), link(link), onTabletBegin(std::move(onTabletBegin)) {
        std::unique_ptr<TournamentWaitView> waitView = std::make_unique<TournamentWaitView>(onDeviceModeChange);
        view = waitView.get();
        activeView = std::move(waitView);
        activeView->initLedDisplay(ledDisplay);
        activeView->initBackDisplay(backDisplay);
        activeView->initEInkDisplay(einkDisplay);
    }

    bool isInMatch() const override {
        return false;
    }

    // Long C leaves too, like the short C in the view.
    bool goBack() override {
        view->exitToMenu();
        return true;
    }

    Garmin::AckStatus handleTabletCommand(const WatchCommand &command) override {
        DeviceModeState mode = DeviceModeState::ModeSwitchingMode;
        const Garmin::AckStatus status = TabletSupport::begin(session, link, command, mode);
        if (status == Garmin::AckStatus::Applied) {
            onTabletBegin(mode);
        }
        return status;
    }

    void loop() override {
        if (activeView) {
            activeView->handleInput(remoteInputManager);
            activeView->renderLedDisplay(ledDisplay);
            activeView->renderBackDisplay(backDisplay);
            activeView->renderEInkDisplay(einkDisplay);
        }
    }
};

#endif //TOURNAMENT_WAIT_MODE_H
