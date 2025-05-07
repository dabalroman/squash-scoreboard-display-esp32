#ifndef MATCH_OVER_VIEW_H
#define MATCH_OVER_VIEW_H

#include "RemoteInputManager.h"
#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/GlyphDisplay.h"
#include "Match/Tournament.h"

#define MATCH_OVER_VIEW_BACK_DISPLAY_PLAYER_CHANGE_MS 2500

class MatchOverView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    MatchRound *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr;
    std::function<void(SquashModeState)> onStateChange;

public:
    MatchOverView(Tournament &tournament, std::function<void(SquashModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = &tournament.getActiveMatch();
        round = &match->getActiveRound();
        playerA = &match->getPlayerA();
        playerB = &match->getPlayerB();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            remoteInputManager.preventTriggerForMs();
            onStateChange(SquashModeState::MatchChoosePlayers);
        }
    }

    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        glyphDisplay.setColonAppearance();

        glyphDisplay.setNumericValue(round->getRealScore(MatchSide::a), round->getRealScore(MatchSide::b));
        glyphDisplay.setGlyphsAppearance(playerA->getColor(), playerB->getColor());

        glyphDisplay.setPlayerAIndicatorAppearance(playerA->getColor(), round->getWinner() == MatchSide::a);
        glyphDisplay.setPlayerBIndicatorAppearance(playerB->getColor(), round->getWinner() == MatchSide::b);

        glyphDisplay.display();
    }

    void renderScreen(BackDisplay &backDisplay) override {
        backDisplay.clear();
        backDisplay.renderScore(round->getRealScore(MatchSide::a), round->getRealScore(MatchSide::b));
        backDisplay.display();
    }
};

#endif //MATCH_OVER_VIEW_H
