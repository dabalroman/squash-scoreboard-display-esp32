#ifndef VOLLEYBALL_MODE__GAME_OVER_VIEW_H
#define VOLLEYBALL_MODE__GAME_OVER_VIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/VolleyballMode/VolleyballModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Tournament/Tournament.h"

class VolleyballGameOverView final : public View {
    Tournament &tournament;
    const Match *match = nullptr;
    const GameResult *gameResult = nullptr;
    UserProfile *playerLeft = nullptr, *playerRight = nullptr;
    uint8_t leftScore = 0, rightScore = 0;
    std::function<void(VolleyballModeState)> onStateChange;

public:
    VolleyballGameOverView(Tournament &tournament, std::function<void(VolleyballModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = tournament.getActiveMatch();

        if (match == nullptr) {
            onStateChange(VolleyballModeState::MatchStartGame);
            return;
        }

        gameResult = match->getLastGameResult();
        if (gameResult == nullptr) {
            onStateChange(VolleyballModeState::MatchStartGame);
            return;
        }

        playerLeft  = &match->getLeftCourtSidePlayer();
        playerRight = &match->getRightCourtSidePlayer();
        leftScore   = playerLeft->getId()  == gameResult->playerAId ? gameResult->playerAScore : gameResult->playerBScore;
        rightScore  = playerRight->getId() == gameResult->playerAId ? gameResult->playerAScore : gameResult->playerBScore;
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonC.takeActionIfPossible() || remoteInputManager.buttonD.takeActionIfPossible()) {
            remoteInputManager.preventTriggerForMs();
            onStateChange(VolleyballModeState::MatchStartGame);

            queueRender();
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        const bool leftWon  = gameResult->winnerPlayerId == playerLeft->getId();
        const bool rightWon = gameResult->winnerPlayerId == playerRight->getId();

        ledDisplay.setColonAppearance();
        ledDisplay.setNumericValue(leftScore, rightScore);
        ledDisplay.setGlyphsAppearance(playerLeft->getColor(), playerRight->getColor());
        ledDisplay.setIndicatorAppearancePlayerA(playerLeft->getColor(), leftWon);
        ledDisplay.setIndicatorAppearancePlayerB(playerRight->getColor(), rightWon);
        ledDisplay.startCelebration(leftWon ? playerLeft->getColor() : playerRight->getColor());
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
        backDisplay.renderScoreWidget(leftScore, rightScore);
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //VOLLEYBALL_MODE__GAME_OVER_VIEW_H
