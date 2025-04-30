#ifndef SQUASHMODE_H
#define SQUASHMODE_H

#include "SquashModeState.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Match/Tournament.h"
#include "Match/Rules/SquashRules.h"
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
                activeView.reset(
                    new TournamentChoosePlayersView(
                        *tournament,
                        users,
                        [this](const SquashModeState newState) { setState(newState); }
                    )
                );
                break;
            case SquashModeState::MatchChoosePlayers:
                activeView.reset(
                    new MatchChoosePlayersView(
                        *tournament,
                        [this](const SquashModeState newState) { setState(newState); }
                    )
                );
                break;
            case SquashModeState::MatchOn:
                activeView.reset(
                    new MatchPlayingView(
                        *tournament,
                        [this](const SquashModeState newState) { setState(newState); }
                    )
                );
                break;
            case SquashModeState::MatchOver:
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
        Adafruit_SSD1306 &backDisplay,
        RemoteInputManager &remoteInputManager,
        std::vector<UserProfile *> &users
    )
        : DeviceMode(glyphDisplay, backDisplay, remoteInputManager), users(users) {
        tournament = new Tournament(squashRules);

        setState(SquashModeState::TournamentChoosePlayers);
    }

    void loop() override {
        if (state != previousState) {
            handleStateChange();
        }

        if (activeView) {
            activeView->handleInput(remoteInputManager);
            activeView->renderGlyphs(glyphDisplay);
            activeView->renderBack(backDisplay);
        }
    }
};

#endif //SQUASHMODE_H
