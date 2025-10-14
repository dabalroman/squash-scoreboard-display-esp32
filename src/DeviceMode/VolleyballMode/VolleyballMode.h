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
    Tournament *tournament;
    std::vector<UserProfile *> &users;
    std::unique_ptr<View> activeView;

    void setState(const VolleyballModeState newState) {
        state = newState;
    }

    void handleStateChange() {
        previousState = state;

        switch (state) {
            case VolleyballModeState::TournamentChoosePlayers:
                backDisplay.initSmallFont();
                activeView.reset(
                    new VolleyballTournamentChoosePlayersView(
                        *tournament,
                        users,
                        onDeviceModeChange,
                        [this](const VolleyballModeState newState) { setState(newState); }
                    )
                );
                break;
            case VolleyballModeState::MatchChoosePlayers:
                backDisplay.initBigFont();
                activeView.reset(
                    new VolleyballMatchChoosePlayersView(
                        *tournament,
                        onDeviceModeChange,
                        [this](const VolleyballModeState newState) { setState(newState); }
                    )
                );
                break;
            case VolleyballModeState::MatchPlaying:
                backDisplay.initBigFont();
                activeView.reset(
                    new VolleyballMatchPlayingView(
                        *tournament,
                        [this](const VolleyballModeState newState) { setState(newState); }
                    )
                );
                break;
            case VolleyballModeState::MatchOver:
                backDisplay.initBigFont();
                activeView.reset(
                    new VolleyballMatchOverView(
                        *tournament,
                        [this](const VolleyballModeState newState) { setState(newState); }
                    ));
                break;
            default:
                printLn("TRIED TO CHANGE TO UNSUPPORTED STATE");
                break;
        }
    }

public:
    VolleyballMode(
        GlyphDisplay &glyphDisplay,
        BackDisplay &backDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::vector<UserProfile *> &users,
        Rules *rules
    )
        : DeviceMode(glyphDisplay, backDisplay, remoteInputManager, onDeviceModeChange), users(users) {
        tournament = new Tournament(*rules);

        glyphDisplay.initForSquashMode();

        setState(VolleyballModeState::TournamentChoosePlayers);
    }

    void loop() override {
        if (state != previousState) {
            handleStateChange();
        }

        if (activeView) {
            activeView->handleInput(remoteInputManager);
            activeView->renderGlyphs(glyphDisplay);
            activeView->renderScreen(backDisplay);
        }
    }
};

#endif //VOLLEYBALL_MODE_H
