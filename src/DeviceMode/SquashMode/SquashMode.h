#ifndef SQUASHMODE_H
#define SQUASHMODE_H

#include "SquashModeState.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Match/Tournament.h"
#include "Match/Rules/SquashRules.h"
#include "Views/TournamentChoosePlayersView.h"

class SquashMode final : public DeviceMode {
    SquashModeState state = SquashModeState::Init;
    SquashRules squashRules;
    Tournament *tournament;
    std::vector<UserProfile *> &users;
    std::unique_ptr<View> activeView;

    void setState(const SquashModeState newState) {
        state = newState;
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
        activeView.reset(new TournamentChoosePlayersView(*tournament, users, [this](const SquashModeState newState) {
            setState(newState);
        }));
    }

    void loop() override {
        if (activeView) {
            activeView->handleInput(remoteInputManager);
            activeView->render(glyphDisplay, backDisplay);
        }
    }
};

#endif //SQUASHMODE_H
