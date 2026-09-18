#ifndef PADEL_MODE__GAME_OVER_VIEW_H
#define PADEL_MODE__GAME_OVER_VIEW_H

#include "Strings.h"
#include "DeviceMode/View.h"
#include "DeviceMode/PadelMode/PadelModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Tournament/Tournament.h"

class PadelGameOverView final : public View {
    Tournament &tournament;
    const Match *match = nullptr;
    const GameResult *gameResult = nullptr;
    UserProfile *playerLeft = nullptr, *playerRight = nullptr;
    uint8_t leftScore = 0, rightScore = 0;
    std::function<void(PadelModeState)> onStateChange;

public:
    PadelGameOverView(Tournament &tournament, std::function<void(PadelModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = tournament.getActiveMatch();

        if (match == nullptr) {
            onStateChange(PadelModeState::MatchStartGame);
            return;
        }

        gameResult = match->getLastGameResult();
        if (gameResult == nullptr) {
            onStateChange(PadelModeState::MatchStartGame);
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
            onStateChange(PadelModeState::MatchStartGame);

            queueRender();
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        if (gameResult == nullptr) return;

        const bool leftWon  = gameResult->winnerPlayerId == playerLeft->getId();
        const bool rightWon = gameResult->winnerPlayerId == playerRight->getId();

        ledDisplay.setColonAppearance();
        ledDisplay.setNumericValue(leftScore, rightScore);
        ledDisplay.setGlyphsAppearance(playerLeft->getColor(), playerRight->getColor());
        ledDisplay.setBorderEnabled(true);
        ledDisplay.setIndicatorAppearancePlayerA(playerLeft->getColor(), leftWon);
        ledDisplay.setIndicatorAppearancePlayerB(playerRight->getColor(), rightWon);
        ledDisplay.setBorderAppearance(playerLeft->getColor(), playerRight->getColor(), leftWon, rightWon);
        ledDisplay.startCelebration(leftWon ? playerLeft->getColor() : playerRight->getColor(), leftWon);
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        // celebrations - update every tick
        if (gameResult == nullptr) return;

        ledDisplay.display();
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        if (gameResult == nullptr) return;

        backDisplay.initBigFont();
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available()) {
            return;   // V1: constant false, the score lookups below fold away
        }

        if (match == nullptr || playerLeft == nullptr || playerRight == nullptr) {
            einkDisplay.showBlank();
            return;
        }

        // Gems of the set that just ended. Sets appear one press later, on
        // MatchStartGame, rather than competing with the gems here.
        einkDisplay.showMatchScore(
            playerLeft->getName(), leftScore,
            playerRight->getName(), rightScore,
            Str::MATCH_SCORE_LABEL_GEMS
        );
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (gameResult == nullptr || !shouldRenderBack) return;

        backDisplay.clear();
        backDisplay.renderScoreWidget(leftScore, rightScore);
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //PADEL_MODE__GAME_OVER_VIEW_H
