#ifndef SQUASH_MODE__MATCH_OVER_VIEW_H
#define SQUASH_MODE__MATCH_OVER_VIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/ScoreHistoryBarAdapter.h"
#include "Match/Tournament.h"

#define MATCH_OVER_VIEW_BACK_DISPLAY_PLAYER_CHANGE_MS 2500

class SquashMatchOverView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    MatchRound *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr;
    std::function<void(SquashModeState)> onStateChange;

public:
    SquashMatchOverView(Tournament &tournament, std::function<void(SquashModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = tournament.getActiveMatch();
        if (!match) {
            return;
        }

        round = &match->getActiveRound();
        playerA = &match->getPlayerA();
        playerB = &match->getPlayerB();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonD.takeActionIfPossible() || remoteInputManager.buttonC.takeActionIfPossible()) {
            remoteInputManager.preventTriggerForMs();
            onStateChange(SquashModeState::MatchChoosePlayers);

            queueRender();
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.setColonAppearance();

        ledDisplay.setNumericValue(round->getRealScore(MatchSide::a), round->getRealScore(MatchSide::b));
        ledDisplay.setGlyphsAppearance(playerA->getColor(), playerB->getColor());

        ledDisplay.setIndicatorAppearancePlayerA(playerA->getColor(), round->getWinner() == MatchSide::a);
        ledDisplay.setIndicatorAppearancePlayerB(playerB->getColor(), round->getWinner() == MatchSide::b);

        ledDisplay.startCelebration(round->getWinner() == MatchSide::a ? playerA->getColor() : playerB->getColor());
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        // celebrations - update every tick

        ledDisplay.display();
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.initBigFont();
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        backDisplay.renderScoreWidget(round->getRealScore(MatchSide::a), round->getRealScore(MatchSide::b));
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //SQUASH_MODE__MATCH_OVER_VIEW_H
