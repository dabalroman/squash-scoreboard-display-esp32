#ifndef SQUASH_MODE_H
#define SQUASH_MODE_H

#include <memory>
#include "SquashModeState.h"
#include "Utils.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Tournament/Tournament.h"
#include "Tournament/Rules/SquashRules.h"
#include "RemoteDevelopmentService/LoggerHelper.h"
#include "Views/SquashMatchStartGameView.h"
#include "Views/SquashGameOverView.h"
#include "Views/SquashGamePlayingView.h"
#include "Views/SquashTournamentChoosePlayersView.h"

class SquashMode final : public DeviceMode {
    SquashModeState state = SquashModeState::Init;
    SquashModeState previousState = SquashModeState::Init;
    Tournament tournament;
    std::vector<UserProfile *> &users;
    std::function<void()> onMatchOver;

    void setState(const SquashModeState newState) {
        state = newState;
    }

    void handleStateChange() {
        previousState = state;

        switch (state) {
            case SquashModeState::TournamentChoosePlayers:
                activeView = std::make_unique<SquashTournamentChoosePlayersView>(
                    tournament,
                    users,
                    onDeviceModeChange,
                    [this](const SquashModeState newState) { setState(newState); }
                );
                break;
            case SquashModeState::MatchStartGame:
                activeView = std::make_unique<SquashMatchStartGameView>(
                    tournament,
                    onDeviceModeChange,
                    [this](const SquashModeState newState) { setState(newState); }
                );
                break;
            case SquashModeState::GamePlaying:
                activeView = std::make_unique<SquashGamePlayingView>(
                    tournament,
                    [this](const SquashModeState newState) { setState(newState); }
                );
                break;
            case SquashModeState::GameOver:
                if (onMatchOver) {
                    onMatchOver();
                }

                activeView = std::make_unique<SquashGameOverView>(
                    tournament,
                    [this](const SquashModeState newState) { setState(newState); }
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
    SquashMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        EInkDisplay &einkDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::vector<UserProfile *> &users,
        std::function<void()> onMatchOver
    )
        : DeviceMode(ledDisplay, backDisplay, einkDisplay, remoteInputManager, onDeviceModeChange),
          tournament(std::make_unique<SquashRules>()),
          users(users), onMatchOver(std::move(onMatchOver)) {

        ledDisplay.setSameSideMode(false);
        backDisplay.setSameSideMode(false);
        setState(SquashModeState::TournamentChoosePlayers);
    }

    bool goBack() override {
        switch (state) {
            case SquashModeState::TournamentChoosePlayers:
                onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
                return true;
            case SquashModeState::MatchStartGame:
                setState(SquashModeState::TournamentChoosePlayers);
                return true;
            case SquashModeState::GameOver:
                // The result is already recorded; back cannot undo it, so it lands where
                // the forward action would.
                setState(SquashModeState::MatchStartGame);
                return true;
            case SquashModeState::GamePlaying:
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

#endif //SQUASH_MODE_H
