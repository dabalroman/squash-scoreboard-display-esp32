#ifndef MATCH_OVER_VIEW_H
#define MATCH_OVER_VIEW_H

#include "Adafruit_SSD1306.h"
#include "RemoteInputManager.h"
#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/GlyphDisplay.h"
#include "Match/Tournament.h"

class MatchOverView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    MatchRound *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr;
    std::function<void(SquashModeState)> onStateChange;

    uint32_t lastPointScoredAtMs = 0;
    MatchSide commitResultWinner = MatchSide::none;

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
            onStateChange(SquashModeState::MatchChoosePlayers);
        }
    }

    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        if (!shouldRenderGlyphs) {
            return;
        }

        glyphDisplay.setColonAppearance();

        glyphDisplay.setNumericValue(round->getRealScore(MatchSide::a), round->getRealScore(MatchSide::b));
        glyphDisplay.setGlyphsAppearance(playerA->getColor(), playerB->getColor());

        glyphDisplay.setPlayersIndicatorsState(true);
        glyphDisplay.setPlayerAIndicatorAppearance(playerA->getColor());
        glyphDisplay.setPlayerBIndicatorAppearance(playerB->getColor());

        glyphDisplay.display();
    }

    void renderBack(Adafruit_SSD1306 &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clearDisplay();
        backDisplay.setFont(&FreeMonoBold24pt7b);
        backDisplay.setCursor(0, 24);
        backDisplay.print(round->getRealScore(MatchSide::a) + ":" + round->getRealScore(MatchSide::b));
        backDisplay.display();
    }
};

#endif //MATCH_OVER_VIEW_H
