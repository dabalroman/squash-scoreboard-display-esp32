#ifndef VOLLEYBALL_MODE_H
#define VOLLEYBALL_MODE_H

#include "VolleyballModeState.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Tournament/Tournament.h"
#include "Tournament/Rules/Rules.h"
#include "RemoteDevelopmentService/LoggerHelper.h"
#include "Views/VolleyballMatchStartGameView.h"
#include "Views/VolleyballGameOverView.h"
#include "DeviceMode/Celebration/GameCelebrationView.h"
#include "DeviceMode/Intro/MatchIntroView.h"
#include "Views/VolleyballGamePlayingView.h"
#include "Views/VolleyballTournamentChoosePlayersView.h"
#include "Utils.h"

enum class VolleyballModeState : uint8_t;

class VolleyballMode final : public DeviceMode {
    VolleyballModeState state = VolleyballModeState::Init;
    VolleyballModeState previousState = VolleyballModeState::Init;
    Tournament tournament;
    std::vector<UserProfile *> &users;
    std::function<void(CelebrationVariant)> onMatchOver;

    void setState(const VolleyballModeState newState) {
        state = newState;
    }

    void handleStateChange() {
        previousState = state;
        // Timer-driven hand-overs (celebration, intro) call no preventTriggerForMs():
        // a press latched in between must not act on the new view's first frame.
        remoteInputManager.clearLatches();

        switch (state) {
            case VolleyballModeState::TournamentChoosePlayers:
                activeView = std::make_unique<VolleyballTournamentChoosePlayersView>(
                    tournament,
                    users,
                    onDeviceModeChange,
                    [this](const VolleyballModeState newState) { setState(newState); }
                );
                break;
            case VolleyballModeState::MatchStartGame:
                activeView = std::make_unique<VolleyballMatchStartGameView>(
                    tournament,
                    onDeviceModeChange,
                    [this](const VolleyballModeState newState) { setState(newState); }
                );
                break;
            case VolleyballModeState::MatchIntro:
                activeView = std::make_unique<MatchIntroView<VolleyballModeState>>(
                    tournament,
                    [this](const VolleyballModeState newState) { setState(newState); },
                    Str::MATCH_SCORE_LABEL_SETS,
                    true
                );
                break;
            case VolleyballModeState::GamePlaying:
                activeView = std::make_unique<VolleyballGamePlayingView>(
                    tournament,
                    [this](const VolleyballModeState newState) { setState(newState); }
                );
                break;
            case VolleyballModeState::GameCelebration: {
                auto view = std::make_unique<GameCelebrationView<VolleyballModeState>>(
                    tournament,
                    [this](const VolleyballModeState newState) { setState(newState); },
                    Str::MATCH_SCORE_LABEL_SETS,
                    false
                );
                // Once per game: restoreView() after an Overlay never comes through here.
                if (onMatchOver) {
                    onMatchOver(view->getVariant());
                }

                activeView = std::move(view);
                break;
            }
            case VolleyballModeState::GameOver:
                activeView = std::make_unique<VolleyballGameOverView>(
                    tournament,
                    [this](const VolleyballModeState newState) { setState(newState); }
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
    VolleyballMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        EInkDisplay &einkDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::vector<UserProfile *> &users,
        std::unique_ptr<Rules> rules,
        std::function<void(CelebrationVariant)> onMatchOver
    )
        : DeviceMode(ledDisplay, backDisplay, einkDisplay, remoteInputManager, onDeviceModeChange),
          tournament(std::move(rules)), users(users), onMatchOver(std::move(onMatchOver)) {

        ledDisplay.setSameSideMode(true);
        backDisplay.setSameSideMode(true);
        setState(VolleyballModeState::TournamentChoosePlayers);
    }

    bool isInMatch() const override {
        return state != VolleyballModeState::TournamentChoosePlayers;
    }

    // setState() swaps the view at the next loop(); until then a watch command gets BUSY.
    bool viewChangePending() const override {
        return state != previousState;
    }

    bool goBack() override {
        switch (state) {
            case VolleyballModeState::TournamentChoosePlayers:
                onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
                return true;
            case VolleyballModeState::MatchStartGame:
                setState(VolleyballModeState::TournamentChoosePlayers);
                return true;
            case VolleyballModeState::MatchIntro:
                // No game exists yet (GamePlaying creates it), so nothing is lost.
                setState(VolleyballModeState::MatchStartGame);
                return true;
            case VolleyballModeState::GameCelebration:
            case VolleyballModeState::GameOver:
                // The result is already recorded; back cannot undo it, so it lands where
                // the forward action would.
                setState(VolleyballModeState::MatchStartGame);
                return true;
            case VolleyballModeState::GamePlaying:
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

#endif //VOLLEYBALL_MODE_H
