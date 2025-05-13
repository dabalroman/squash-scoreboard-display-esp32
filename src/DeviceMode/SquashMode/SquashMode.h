#ifndef SQUASH_MODE_H
#define SQUASH_MODE_H

#include "SquashModeState.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Match/Tournament.h"
#include "Match/Rules/SquashRules.h"
#include "RemoteDevelopmentService/LoggerHelper.h"
#include "Views/MatchChoosePlayersView.h"
#include "Views/MatchOverView.h"
#include "Views/MatchPlayingView.h"
#include "Views/TournamentChoosePlayersView.h"

class SquashMode final : public DeviceMode {
    SquashModeState state = SquashModeState::Init;
    SquashModeState previousState = SquashModeState::Init;
    SquashRules squashRules;
    Tournament *tournament;
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
                activeView.reset(
                    new TournamentChoosePlayersView(
                        *tournament,
                        users,
                        onDeviceModeChange,
                        [this](const SquashModeState newState) { setState(newState); }
                    )
                );
                break;
            case SquashModeState::MatchChoosePlayers:
                backDisplay.initBigFont();
                activeView.reset(
                    new MatchChoosePlayersView(
                        *tournament,
                        [this](const SquashModeState newState) { setState(newState); }
                    )
                );
                break;
            case SquashModeState::MatchPlaying:
                backDisplay.initBigFont();
                activeView.reset(
                    new MatchPlayingView(
                        *tournament,
                        [this](const SquashModeState newState) { setState(newState); }
                    )
                );
                break;
            case SquashModeState::MatchOver:
                backDisplay.initBigFont();
                activeView.reset(
                    new MatchOverView(
                        *tournament,
                        [this](const SquashModeState newState) { setState(newState); }
                    ));
                break;
            default:
                printLn("TRIED TO CHANGE TO UNSUPPORTED STATE");
                break;
        }
    }

public:
    SquashMode(
        GlyphDisplay &glyphDisplay,
        BackDisplay &backDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::vector<UserProfile *> &users
    )
        : DeviceMode(glyphDisplay, backDisplay, remoteInputManager, onDeviceModeChange), users(users) {
        tournament = new Tournament(squashRules);

        glyphDisplay.initForSquashMode();

        setState(SquashModeState::TournamentChoosePlayers);
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

#endif //SQUASH_MODE_H
