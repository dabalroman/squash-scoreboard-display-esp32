#ifndef VOLLEYBALL_MODE_H
#define VOLLEYBALL_MODE_H

#include "VolleyballModeState.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Match/Tournament.h"
#include "Match/Rules/Rules.h"
#include "RemoteDevelopmentService/LoggerHelper.h"
#include "Views/VolleyballMatchChoosePlayersView.h"
#include "Views/VolleyballMatchOverView.h"
#include "Views/VolleyballMatchPlayingView.h"
#include "Views/VolleyballTournamentChoosePlayersView.h"

enum class VolleyballModeState : uint8_t;

class VolleyballMode final : public DeviceMode {
    VolleyballModeState state = VolleyballModeState::Init;
    VolleyballModeState previousState = VolleyballModeState::Init;
    Tournament tournament;
    std::vector<UserProfile *> &users;
    std::unique_ptr<View> activeView;
    std::function<void()> onMatchOver;

    void setState(const VolleyballModeState newState) {
        state = newState;
    }

    void handleStateChange() {
        previousState = state;

        switch (state) {
            case VolleyballModeState::TournamentChoosePlayers:
                activeView = std::make_unique<VolleyballTournamentChoosePlayersView>(
                    tournament,
                    users,
                    onDeviceModeChange,
                    [this](const VolleyballModeState newState) { setState(newState); }
                );
                break;
            case VolleyballModeState::MatchChoosePlayers:
                activeView = std::make_unique<VolleyballMatchChoosePlayersView>(
                    tournament,
                    onDeviceModeChange,
                    [this](const VolleyballModeState newState) { setState(newState); }
                );
                break;
            case VolleyballModeState::MatchPlaying:
                activeView = std::make_unique<VolleyballMatchPlayingView>(
                    tournament,
                    [this](const VolleyballModeState newState) { setState(newState); }
                );
                break;
            case VolleyballModeState::MatchOver:
                if (onMatchOver) {
                    onMatchOver();
                }

                activeView = std::make_unique<VolleyballMatchOverView>(
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
    }

public:
    VolleyballMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::vector<UserProfile *> &users,
        std::unique_ptr<Rules> rules,
        std::function<void()> onMatchOver
    )
        : DeviceMode(ledDisplay, backDisplay, remoteInputManager, onDeviceModeChange),
          tournament(std::move(rules)), users(users), onMatchOver(std::move(onMatchOver)) {
        ledDisplay.initForSquashMode();

        setState(VolleyballModeState::TournamentChoosePlayers);
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

#endif //VOLLEYBALL_MODE_H
