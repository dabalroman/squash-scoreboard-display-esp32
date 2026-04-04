#ifndef SQUASH_MODE_H
#define SQUASH_MODE_H

#include <memory>
#include "SquashModeState.h"
#include "Utils.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Match/Tournament.h"
#include "Match/Rules/SquashRules.h"
#include "RemoteDevelopmentService/LoggerHelper.h"
#include "Views/SquashMatchChoosePlayersView.h"
#include "Views/SquashMatchOverView.h"
#include "Views/SquashMatchPlayingView.h"
#include "Views/SquashTournamentChoosePlayersView.h"

class SquashMode final : public DeviceMode {
    SquashModeState state = SquashModeState::Init;
    SquashModeState previousState = SquashModeState::Init;
    Tournament tournament;
    std::vector<UserProfile *> &users;
    std::unique_ptr<View> activeView;

    void setState(const SquashModeState newState) {
        state = newState;
    }

    void handleStateChange() {
        previousState = state;

        switch (state) {
            case SquashModeState::TournamentChoosePlayers:
                backDisplay.initSmallFont();
                activeView = std::make_unique<SquashTournamentChoosePlayersView>(
                    tournament,
                    users,
                    onDeviceModeChange,
                    [this](const SquashModeState newState) { setState(newState); }
                );
                break;
            case SquashModeState::MatchChoosePlayers:
                backDisplay.initBigFont();
                activeView = std::make_unique<SquashMatchChoosePlayersView>(
                    tournament,
                    onDeviceModeChange,
                    [this](const SquashModeState newState) { setState(newState); }
                );
                break;
            case SquashModeState::MatchPlaying:
                backDisplay.initBigFont();
                activeView = std::make_unique<SquashMatchPlayingView>(
                    tournament,
                    [this](const SquashModeState newState) { setState(newState); }
                );
                break;
            case SquashModeState::MatchOver:
                backDisplay.initBigFont();
                activeView = std::make_unique<SquashMatchOverView>(
                    tournament,
                    [this](const SquashModeState newState) { setState(newState); }
                );
                break;
            default:
                printLn("TRIED TO CHANGE TO UNSUPPORTED STATE");
                break;
        }
    }

public:
    SquashMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::vector<UserProfile *> &users
    )
        : DeviceMode(ledDisplay, backDisplay, remoteInputManager, onDeviceModeChange),
          tournament(std::make_unique<SquashRules>()),
          users(users) {
        ledDisplay.initForSquashMode();

        setState(SquashModeState::TournamentChoosePlayers);
    }

    void loop() override {
        if (state != previousState) {
            handleStateChange();
        }

        if (activeView) {
            activeView->handleInput(remoteInputManager);
            activeView->renderLedDisplay(ledDisplay);
            activeView->renderScreen(backDisplay);
        }
    }
};

#endif //SQUASH_MODE_H
