#ifndef MATCH_OVER_VIEW_H
#define MATCH_OVER_VIEW_H

#include "RemoteInputManager.h"
#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/GlyphDisplay.h"
#include "Match/Tournament.h"

#define MATCH_OVER_VIEW_BACK_DISPLAY_PLAYER_CHANGE_MS 5000

class MatchOverView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    MatchRound *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr;
    std::function<void(SquashModeState)> onStateChange;

    uint32_t lastBackDisplayPlayerChangeMs = 0;
    MatchSide lastBackDisplayPlayerSide = MatchSide::a;

public:
    MatchOverView(Tournament &tournament, std::function<void(SquashModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = &tournament.getActiveMatch();
        round = &match->getActiveRound();
        playerA = &match->getPlayerA();
        playerB = &match->getPlayerB();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        const uint32_t now = millis();

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            onStateChange(SquashModeState::MatchChoosePlayers);
        }

        if (lastBackDisplayPlayerChangeMs + MATCH_OVER_VIEW_BACK_DISPLAY_PLAYER_CHANGE_MS <= now) {
            lastBackDisplayPlayerChangeMs = millis();
            lastBackDisplayPlayerSide = lastBackDisplayPlayerSide == MatchSide::a ? MatchSide::b : MatchSide::a;
        }
    }

    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        glyphDisplay.setColonAppearance();

        glyphDisplay.setNumericValue(round->getRealScore(MatchSide::a), round->getRealScore(MatchSide::b));
        glyphDisplay.setGlyphsAppearance(playerA->getColor(), playerB->getColor());

        // Standard display, cycle between players
        if (lastBackDisplayPlayerSide == MatchSide::a) {
            glyphDisplay.setPlayerAIndicatorAppearance(playerA->getColor(), false);
            glyphDisplay.setPlayerBIndicatorAppearance(Colors::Black, false);
        } else {
            glyphDisplay.setPlayerAIndicatorAppearance(Colors::Black, false);
            glyphDisplay.setPlayerBIndicatorAppearance(playerB->getColor(), false);
        }

        glyphDisplay.display();
    }

    void renderScreen(BackDisplay &backDisplay) override {
        backDisplay.clear();
        backDisplay.setCursorTo2CharCenter();

        backDisplay.setBlinking(round->getWinner() == lastBackDisplayPlayerSide);
        backDisplay.renderScore(round->getRealScore(lastBackDisplayPlayerSide));

        backDisplay.display();
    }
};

#endif //MATCH_OVER_VIEW_H
