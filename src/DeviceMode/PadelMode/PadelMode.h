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
#include "DeviceMode/Celebration/GameCelebrationView.h"
#include "DeviceMode/Intro/MatchIntroView.h"
#include "Views/PadelGamePlayingView.h"
#include "Views/PadelTournamentChoosePlayersView.h"

class PadelMode final : public DeviceMode {
    PadelModeState state = PadelModeState::Init;
    PadelModeState previousState = PadelModeState::Init;
    Tournament tournament;
    std::vector<UserProfile *> &users;
    std::function<void(CelebrationVariant)> onMatchOver;

    void setState(const PadelModeState newState) {
        state = newState;
    }

    void handleStateChange() {
        previousState = state;
        // Timer-driven hand-overs (celebration, intro) call no preventTriggerForMs():
        // a press latched in between must not act on the new view's first frame.
        remoteInputManager.clearLatches();

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
            case PadelModeState::MatchIntro:
                activeView = std::make_unique<MatchIntroView<PadelModeState>>(
                    tournament,
                    [this](const PadelModeState newState) { setState(newState); },
                    Str::MATCH_SCORE_LABEL_GEMS,
                    false
                );
                break;
            case PadelModeState::GamePlaying:
                activeView = std::make_unique<PadelGamePlayingView>(
                    tournament,
                    [this](const PadelModeState newState) { setState(newState); }
                );
                break;
            case PadelModeState::GameCelebration: {
                auto view = std::make_unique<GameCelebrationView<PadelModeState>>(
                    tournament,
                    [this](const PadelModeState newState) { setState(newState); },
                    Str::MATCH_SCORE_LABEL_GEMS,
                    true
                );
                // Once per game: restoreView() after an Overlay never comes through here.
                if (onMatchOver) {
                    onMatchOver(view->getVariant());
                }

                activeView = std::move(view);
                break;
            }
            case PadelModeState::GameOver:
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
        std::function<void(CelebrationVariant)> onMatchOver
    )
        : DeviceMode(ledDisplay, backDisplay, einkDisplay, remoteInputManager, onDeviceModeChange),
          tournament(std::make_unique<PadelRules>()),
          users(users), onMatchOver(std::move(onMatchOver)) {

        ledDisplay.setSameSideMode(true);
        backDisplay.setSameSideMode(true);
        setState(PadelModeState::TournamentChoosePlayers);
    }

    bool isInMatch() const override {
        return state != PadelModeState::TournamentChoosePlayers;
    }

    // setState() swaps the view at the next loop(); until then a watch command gets BUSY.
    bool viewChangePending() const override {
        return state != previousState;
    }

    bool goBack() override {
        switch (state) {
            case PadelModeState::TournamentChoosePlayers:
                onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
                return true;
            case PadelModeState::MatchStartGame:
                setState(PadelModeState::TournamentChoosePlayers);
                return true;
            case PadelModeState::MatchIntro:
                // No game exists yet (GamePlaying creates it), so nothing is lost.
                setState(PadelModeState::MatchStartGame);
                return true;
            case PadelModeState::GameCelebration:
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
            // A swap the input requested lands next loop, and the outgoing view must not draw
            // again: a winning commit's finishGame() has freed the Game it points at.
            if (state != previousState) {
                return;
            }
            activeView->renderLedDisplay(ledDisplay);
            activeView->renderBackDisplay(backDisplay);
            activeView->renderEInkDisplay(einkDisplay);
        }
    }
};

#endif //PADEL_MODE_H
