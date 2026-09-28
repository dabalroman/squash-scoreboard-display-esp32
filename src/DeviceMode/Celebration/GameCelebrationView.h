#ifndef GAME_CELEBRATION_VIEW_H
#define GAME_CELEBRATION_VIEW_H

#include <functional>
#include "DeviceMode/View.h"
#include "DeviceMode/Celebration/CelebrationVariant.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Tournament/Tournament.h"

/**
 * Between GamePlaying and the GameOver summary, shared by every sport mode.
 * Shows exactly what the summary shows (so the hand-over repaints nothing on
 * the e-paper) with the variant's animation on top, and leaves for GameOver
 * when that animation ends or on C/D.
 */
template <typename StateEnum>
class GameCelebrationView final : public View {
    const Match *match = nullptr;
    const GameResult *gameResult = nullptr;
    UserProfile *playerLeft = nullptr, *playerRight = nullptr;
    uint8_t leftScore = 0, rightScore = 0;
    std::function<void(StateEnum)> onStateChange;
    const char *einkLabel;
    bool einkShowsGameScore;
    CelebrationVariant variant = CelebrationVariant::Normal;
    bool started = false;

public:
    GameCelebrationView(
        Tournament &tournament,
        std::function<void(StateEnum)> onStateChange,
        const char *einkLabel,
        const bool einkShowsGameScore
    )
        : onStateChange(std::move(onStateChange)), einkLabel(einkLabel), einkShowsGameScore(einkShowsGameScore) {
        match = tournament.getActiveMatch();
        if (match != nullptr) {
            gameResult = match->getLastGameResult();
        }

        if (gameResult == nullptr) {
            this->onStateChange(StateEnum::MatchStartGame);
            return;
        }

        playerLeft  = &match->getLeftCourtSidePlayer();
        playerRight = &match->getRightCourtSidePlayer();
        leftScore   = playerLeft->getId()  == gameResult->playerAId ? gameResult->playerAScore : gameResult->playerBScore;
        rightScore  = playerRight->getId() == gameResult->playerAId ? gameResult->playerAScore : gameResult->playerBScore;
        variant     = selectCelebrationVariant(*match, *gameResult);
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonC.takeActionIfPossible() || remoteInputManager.buttonD.takeActionIfPossible()) {
            // Otherwise the same press also leaves the summary.
            remoteInputManager.preventTriggerForMs();
            onStateChange(StateEnum::GameOver);
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
        // No resetAnimations() here (it would stop the sweep), so game-ball breathing is dropped by hand.
        ledDisplay.setBreathing(0);

        // A second init is restoreView() after an Overlay, which stopped the
        // sweep: no replay, renderLedDisplay hands over to the summary.
        if (started) return;
        started = true;

        switch (variant) {
            case CelebrationVariant::Normal:
            default:
                ledDisplay.startCelebration(leftWon ? playerLeft->getColor() : playerRight->getColor(), leftWon);
                break;
        }
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        // Also keeps a constructor bail to MatchStartGame from being overwritten.
        if (gameResult == nullptr) return;

        ledDisplay.display();

        if (!ledDisplay.celebrationActive()) {
            onStateChange(StateEnum::GameOver);
        }
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        if (gameResult == nullptr) return;

        backDisplay.initBigFont();
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (gameResult == nullptr || !shouldRenderBack) return;

        backDisplay.clear();
        backDisplay.renderScoreWidget(leftScore, rightScore);
        backDisplay.display();

        shouldRenderBack = false;
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available()) return;

        if (gameResult == nullptr) {
            einkDisplay.showBlank();
            return;
        }

        if (einkShowsGameScore) {
            einkDisplay.showMatchScore(
                playerLeft->getName(), leftScore,
                playerRight->getName(), rightScore,
                einkLabel
            );
            return;
        }

        const MatchResult result = match->getMatchResult();
        einkDisplay.showMatchScore(
            playerLeft->getName(), result.scoreOf(playerLeft->getId()),
            playerRight->getName(), result.scoreOf(playerRight->getId()),
            einkLabel
        );
    }
};

#endif //GAME_CELEBRATION_VIEW_H
