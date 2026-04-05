#ifndef SQUASH_MODE__GAME_OVER_VIEW_H
#define SQUASH_MODE__GAME_OVER_VIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/ScoreHistoryBarAdapter.h"
#include "Tournament/Tournament.h"

class SquashGameOverView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    Game *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr;
    std::function<void(SquashModeState)> onStateChange;

public:
    SquashGameOverView(Tournament &tournament, std::function<void(SquashModeState)> onStateChange)
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
            onStateChange(SquashModeState::MatchStartGame);

            queueRender();
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.setColonAppearance();

        ledDisplay.setNumericValue(round->getRealScore(GameSide::a), round->getRealScore(GameSide::b));
        ledDisplay.setGlyphsAppearance(playerA->getColor(), playerB->getColor());

        ledDisplay.setIndicatorAppearancePlayerA(playerA->getColor(), round->getWinner() == GameSide::a);
        ledDisplay.setIndicatorAppearancePlayerB(playerB->getColor(), round->getWinner() == GameSide::b);

        ledDisplay.startCelebration(round->getWinner() == GameSide::a ? playerA->getColor() : playerB->getColor());
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
        backDisplay.renderScoreWidget(round->getRealScore(GameSide::a), round->getRealScore(GameSide::b));
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //SQUASH_MODE__GAME_OVER_VIEW_H
