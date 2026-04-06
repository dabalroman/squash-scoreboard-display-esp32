#ifndef SQUASH_MODE__GAME_OVER_VIEW_H
#define SQUASH_MODE__GAME_OVER_VIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Tournament/Tournament.h"

class SquashGameOverView final : public View {
    Tournament &tournament;
    const Match *match = nullptr;
    const GameResult *gameResult = nullptr;
    UserProfile *playerLeft = nullptr, *playerRight = nullptr;
    std::function<void(SquashModeState)> onStateChange;

public:
    SquashGameOverView(Tournament &tournament, std::function<void(SquashModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = tournament.getActiveMatch();

        if (match == nullptr) {
            onStateChange(SquashModeState::MatchStartGame);
            return;
        }

        gameResult = match->getLastGameResult();
        if (gameResult == nullptr) {
            onStateChange(SquashModeState::MatchStartGame);
            return;
        }

        playerLeft = &match->getLeftCourtSidePlayer();
        playerRight = &match->getRightCourtSidePlayer();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonC.takeActionIfPossible() || remoteInputManager.buttonD.takeActionIfPossible()) {
            remoteInputManager.preventTriggerForMs();
            onStateChange(SquashModeState::MatchStartGame);

            queueRender();
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.setColonAppearance();

        ledDisplay.setNumericValue(gameResult->playerAScore, gameResult->playerBScore);
        ledDisplay.setGlyphsAppearance(playerLeft->getColor(), playerRight->getColor());

        ledDisplay.setIndicatorAppearancePlayerA(playerLeft->getColor(), gameResult->winner == GameSide::a);
        ledDisplay.setIndicatorAppearancePlayerB(playerRight->getColor(), gameResult->winner == GameSide::b);

        ledDisplay.startCelebration(gameResult->winner == GameSide::a ? playerLeft->getColor() : playerRight->getColor());
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
        backDisplay.renderScoreWidget(gameResult->playerAScore, gameResult->playerBScore);
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //SQUASH_MODE__GAME_OVER_VIEW_H
