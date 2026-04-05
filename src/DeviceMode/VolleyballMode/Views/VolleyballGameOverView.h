#ifndef VOLLEYBALL_MODE__GAME_OVER_VIEW_H
#define VOLLEYBALL_MODE__GAME_OVER_VIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/VolleyballMode/VolleyballModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Tournament/Tournament.h"

class VolleyballGameOverView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    Game *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr;
    std::function<void(VolleyballModeState)> onStateChange;

    bool shouldUpdateLedBarHistoryState = true;

public:
    VolleyballGameOverView(Tournament &tournament, std::function<void(VolleyballModeState)> onStateChange)
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
        if (remoteInputManager.buttonC.takeActionIfPossible() || remoteInputManager.buttonD.takeActionIfPossible()) {
            remoteInputManager.preventTriggerForMs();
            onStateChange(VolleyballModeState::MatchStartGame);

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

#endif //VOLLEYBALL_MODE__GAME_OVER_VIEW_H
