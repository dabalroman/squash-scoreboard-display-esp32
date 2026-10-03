#ifndef GAME_CELEBRATION_VIEW_H
#define GAME_CELEBRATION_VIEW_H

#include <functional>
#include "DeviceMode/View.h"
#include "DeviceMode/WatchSupport.h"
#include "DeviceMode/Celebration/CelebrationVariant.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/EInk/Images/Bajgiel.h"
#include "Display/Images/BajgielOled.h"
#include "Tournament/Tournament.h"

/**
 * Between GamePlaying and the GameOver summary, shared by every sport mode.
 * Shows exactly what the summary shows (so the hand-over repaints nothing on
 * the e-paper) with the variant's animation on top, and leaves for GameOver
 * when that animation ends or on C/D. Bajgiel is the exception: its own
 * picture on the OLED and e-paper, so the e-paper repaints at the hand-over.
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

    CelebrationVariant getVariant() const {
        return variant;
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonC.takeActionIfPossible() || remoteInputManager.buttonD.takeActionIfPossible()) {
            // Otherwise the same press also leaves the summary.
            remoteInputManager.preventTriggerForMs();
            skip();
        }
    }

    void skip() {
        onStateChange(StateEnum::GameOver);
    }

    Garmin::AckStatus handleWatchCommand(const WatchCommand &command) override {
        if (gameResult == nullptr) {
            return Garmin::AckStatus::Busy;
        }

        if (command.id != Garmin::CommandId::Skip) {
            return Garmin::AckStatus::WrongScreen;
        }

        skip();
        return Garmin::AckStatus::Applied;
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
            case CelebrationVariant::Bajgiel:
                ledDisplay.startBajgiel(!leftWon);
                break;
            case CelebrationVariant::Normal:
            default:
                ledDisplay.startCelebration(leftWon ? playerLeft->getColor() : playerRight->getColor(), leftWon);
                break;
        }
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        // Also keeps a constructor bail to MatchStartGame from being overwritten.
        if (gameResult == nullptr) return;

        // Hand over without drawing: the summary's first frame follows directly.
        if (!ledDisplay.celebrationActive()) {
            onStateChange(StateEnum::GameOver);
            return;
        }

        ledDisplay.display();
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        if (gameResult == nullptr) return;

        backDisplay.initBigFont();
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (gameResult == nullptr || !shouldRenderBack) return;

        backDisplay.clear();
        if (variant == CelebrationVariant::Bajgiel) {
            backDisplay.drawBitmap(BAJGIELOLED_BITMAP);
        } else {
            backDisplay.renderScoreWidget(leftScore, rightScore);
        }
        backDisplay.display();

        shouldRenderBack = false;
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available()) return;

        if (gameResult == nullptr) {
            einkDisplay.showBlank();
            return;
        }

        if (variant == CelebrationVariant::Bajgiel) {
            // The summary's showMatchScore repaints the score after the hand-over.
            einkDisplay.showImage(BAJGIEL_EINK_BITMAP);
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

    void describeForWatch(WatchState &state) const override {
        if (gameResult == nullptr) {
            View::describeForWatch(state);
            return;
        }

        state.setCelebration(WatchSupport::result(*match, *gameResult, *playerLeft, *playerRight, leftScore, rightScore));
    }
};

#endif //GAME_CELEBRATION_VIEW_H
