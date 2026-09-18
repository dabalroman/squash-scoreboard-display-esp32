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
        activeView->initEInkDisplay(einkDisplay);
    }

public:
    PadelMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        EInkDisplay &einkDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::vector<UserProfile *> &users,
        std::function<void()> onMatchOver
    )
        : DeviceMode(ledDisplay, backDisplay, einkDisplay, remoteInputManager, onDeviceModeChange),
          tournament(std::make_unique<PadelRules>()),
          users(users), onMatchOver(std::move(onMatchOver)) {

        ledDisplay.setSameSideMode(true);
        backDisplay.setSameSideMode(true);
        setState(PadelModeState::TournamentChoosePlayers);
    }

    bool goBack() override {
        switch (state) {
            case PadelModeState::TournamentChoosePlayers:
                onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
                return true;
            case PadelModeState::MatchStartGame:
                setState(PadelModeState::TournamentChoosePlayers);
                return true;
            case PadelModeState::GameOver:
                // The result is already recorded; back cannot undo it, so it lands where
                // the forward action would.
                setState(PadelModeState::MatchStartGame);
                return true;
            case PadelModeState::GamePlaying:
                // Deliberate, not a missing case: a hold must never discard a live game.
                return false;
            default:
                return false;
        }
    }

    void loop() override {
        if (state != previousState) {
            handleStateChange();
        }

        if (activeView) {
            activeView->handleInput(remoteInputManager);
            activeView->renderLedDisplay(ledDisplay);
            activeView->renderBackDisplay(backDisplay);
            activeView->renderEInkDisplay(einkDisplay);
        }
    }
};

#endif //PADEL_MODE_H
