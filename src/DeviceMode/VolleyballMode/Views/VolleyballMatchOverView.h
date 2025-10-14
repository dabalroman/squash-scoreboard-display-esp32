#ifndef VOLLEYBALL_MODE__MATCH_OVER_VIEW_H
#define VOLLEYBALL_MODE__MATCH_OVER_VIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/VolleyballMode/VolleyballModeState.h"
#include "Display/GlyphDisplay.h"
#include "Match/Tournament.h"

#define MATCH_OVER_VIEW_BACK_DISPLAY_PLAYER_CHANGE_MS 2500

class VolleyballMatchOverView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    MatchRound *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr;
    std::function<void(VolleyballModeState)> onStateChange;

public:
    VolleyballMatchOverView(Tournament &tournament, std::function<void(VolleyballModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = &tournament.getActiveMatch();
        round = &match->getActiveRound();
        playerA = &match->getPlayerA();
        playerB = &match->getPlayerB();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            remoteInputManager.preventTriggerForMs();
            onStateChange(VolleyballModeState::MatchChoosePlayers);
        }
    }

    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        glyphDisplay.setColonAppearance();

        glyphDisplay.setNumericValue(round->getRealScore(MatchSide::a), round->getRealScore(MatchSide::b));
        glyphDisplay.setGlyphsAppearance(playerA->getColor(), playerB->getColor());

        glyphDisplay.setIndicatorAppearancePlayerA(playerA->getColor(), round->getWinner() == MatchSide::a);
        glyphDisplay.setIndicatorAppearancePlayerB(playerB->getColor(), round->getWinner() == MatchSide::b);

        glyphDisplay.display();
    }

    void renderScreen(BackDisplay &backDisplay) override {
        backDisplay.clear();
        backDisplay.renderScoreWidget(round->getRealScore(MatchSide::a), round->getRealScore(MatchSide::b));
        backDisplay.display();
    }
};

#endif //VOLLEYBALL_MODE__MATCH_OVER_VIEW_H
