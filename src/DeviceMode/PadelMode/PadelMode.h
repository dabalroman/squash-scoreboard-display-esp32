#ifndef PADEL_MODE_H
#define PADEL_MODE_H

#include <memory>
#include "PadelModeState.h"
#include "Utils.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Tournament/Tournament.h"
#include "Tournament/Rules/PadelRules.h"
#include "RemoteDevelopmentService/LoggerHelper.h"
#include "Views/PadelMatchStartGameView.h"
#include "Views/PadelGameOverView.h"
#include "Views/PadelGamePlayingView.h"
#include "Views/PadelTournamentChoosePlayersView.h"

class PadelMode final : public DeviceMode {
    PadelModeState state = PadelModeState::Init;
    PadelModeState previousState = PadelModeState::Init;
    Tournament tournament;
    std::vector<UserProfile *> &users;
    std::unique_ptr<View> activeView;
    std::function<void()> onMatchOver;

    void setState(const PadelModeState newState) {
        state = newState;
    }

    void handleStateChange() {
        previousState = state;

        switch (state) {
            case PadelModeState::TournamentChoosePlayers:
                activeView = std::make_unique<PadelTournamentChoosePlayersView>(
                    tournament,
                    users,
                    onDeviceModeChange,
                    [this](const PadelModeState newState) { setState(newState); }
                );
                break;
            case PadelModeState::MatchStartGame:
                activeView = std::make_unique<PadelMatchStartGameView>(
                    tournament,
                    onDeviceModeChange,
                    [this](const PadelModeState newState) { setState(newState); }
                );
                break;
            case PadelModeState::GamePlaying:
                activeView = std::make_unique<PadelGamePlayingView>(
                    tournament,
                    [this](const PadelModeState newState) { setState(newState); }
                );
                break;
            case PadelModeState::GameOver:
                if (onMatchOver) {
                    onMatchOver();
                }

                activeView = std::make_unique<PadelGameOverView>(
                    tournament,
                    [this](const PadelModeState newState) { setState(newState); }
                );
                break;
            default:
                printLn("TRIED TO CHANGE TO UNSUPPORTED STATE");
                break;
        }

        activeView->initLedDisplay(ledDisplay);
        activeView->initBackDisplay(backDisplay);
    }

public:
    PadelMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::vector<UserProfile *> &users,
        std::function<void()> onMatchOver
    )
        : DeviceMode(ledDisplay, backDisplay, remoteInputManager, onDeviceModeChange),
          tournament(std::make_unique<PadelRules>()),
          users(users), onMatchOver(std::move(onMatchOver)) {

        ledDisplay.setSameSideMode(true);
        backDisplay.setSameSideMode(true);
        setState(PadelModeState::TournamentChoosePlayers);
    }

    void loop() override {
        if (state != previousState) {
            handleStateChange();
        }

        if (activeView) {
            activeView->handleInput(remoteInputManager);
            activeView->renderLedDisplay(ledDisplay);
            activeView->renderBackDisplay(backDisplay);
        }
    }
};

#endif //PADEL_MODE_H
