#ifndef SQUASH_MODE__GAME_PLAYING_VIEW_H
#define SQUASH_MODE__GAME_PLAYING_VIEW_H

#include "Strings.h"
#include "DeviceMode/View.h"
#include "DeviceMode/WatchSupport.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "../../../Display/LedDisplay/Renderer/GameScoreHistoryBarRenderer.h"
#include "Tournament/Tournament.h"

class SquashGamePlayingView final : public View {
    constexpr static uint32_t COMMIT_TIMEOUT_MS = 4000;

    Tournament &tournament;
    Match *match = nullptr;
    Game *game = nullptr;
    UserProfile *playerLeft = nullptr, *playerRight = nullptr, *lastPointScoredBy = nullptr;
    std::function<void(SquashModeState)> onStateChange;

    uint32_t lastPointScoredAtMs = 0;
    GameSide commitResultWinner = GameSide::none;

    bool shouldUpdateLedBarState = true;

public:
    SquashGamePlayingView(Tournament &tournament, std::function<void(SquashModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = tournament.getActiveMatch();

        if (match == nullptr) {
            onStateChange(SquashModeState::MatchStartGame);
            return;
        }

        game = match->createGame();
        playerLeft = &match->getLeftCourtSidePlayer();
        playerRight = &match->getRightCourtSidePlayer();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        const uint32_t now = millis();
        bool checkExit = false;

        if (remoteInputManager.buttonA.takeActionIfPossible(750)) {
            score(GameSide::a, now);
        }

        if (remoteInputManager.buttonB.takeActionIfPossible(750)) {
            score(GameSide::b, now);
        }

        if (remoteInputManager.buttonC.takeActionIfPossible(750)) {
            if (undo(GameSide::a, now)) {
                checkExit = true;
            }
        }

        if (remoteInputManager.buttonD.takeActionIfPossible(750)) {
            if (undo(GameSide::b, now)) {
                checkExit = true;
            }
        }

        if (checkExit) {
            leaveToMatchStart();
            return;
        }

        // Handle score commits
        if (game->hasUncommitedPoints() && (now - lastPointScoredAtMs) >= COMMIT_TIMEOUT_MS) {
            commitResultWinner = game->commit();
            shouldUpdateLedBarState = true;

            if (commitResultWinner != GameSide::none) {
                remoteInputManager.preventTriggerForMs();
                match->finishGame();
                game = nullptr; // finishGame() freed it; the view lives until the next loop()
                onStateChange(SquashModeState::GameCelebration);
                return;
            }
        }
    }

    // GameSide::a is the left court player in this view.
    void score(const GameSide side, const uint32_t now) {
        game->scorePoint(side);
        lastPointScoredBy = side == GameSide::a ? &match->getLeftCourtSidePlayer() : &match->getRightCourtSidePlayer();
        lastPointScoredAtMs = now;
        shouldUpdateLedBarState = true;
    }

    // True when the undo came at 0:0: the caller leaves for MatchStartGame.
    bool undo(const GameSide side, const uint32_t now) {
        const bool atZero = game->getTemporaryScore(GameSide::a) == 0 && game->getTemporaryScore(GameSide::b) == 0;

        game->losePoint(side);
        lastPointScoredAtMs = now;
        shouldUpdateLedBarState = true;
        return atZero;
    }

    void leaveToMatchStart() {
        onStateChange(SquashModeState::MatchStartGame);
    }

    Garmin::AckStatus handleWatchCommand(const WatchCommand &command) override {
        if (game == nullptr) {
            return Garmin::AckStatus::Busy;
        }

        switch (command.id) {
            case Garmin::CommandId::Score:
                if (!command.hasValidSide()) {
                    return Garmin::AckStatus::Invalid;
                }
                score(WatchSupport::sideOf(command), millis());
                return Garmin::AckStatus::Applied;
            case Garmin::CommandId::Undo:
                if (!command.hasValidSide()) {
                    return Garmin::AckStatus::Invalid;
                }
                if (undo(WatchSupport::sideOf(command), millis())) {
                    leaveToMatchStart();
                }
                return Garmin::AckStatus::Applied;
            default:
                return Garmin::AckStatus::WrongScreen;
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.resetAnimations();
        ledDisplay.setPlayersIndicatorsState(true);
        ledDisplay.setBorderEnabled(true);
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        // blinking - update every tick

        if (lastPointScoredBy == nullptr) {
            ledDisplay.setColonAppearance(Colors::White, true);
        } else {
            ledDisplay.setColonAppearance(lastPointScoredBy->getColor(), true);
        }

        ledDisplay.setNumericValue(game->getTemporaryScore(GameSide::a), game->getTemporaryScore(GameSide::b));
        ledDisplay.setGlyphsAppearance(
            playerLeft->getColor(),
            playerRight->getColor(),
            game->hasUncommitedPoints(GameSide::a),
            game->hasUncommitedPoints(GameSide::b)
        );

        ledDisplay.setIndicatorAppearancePlayerA(playerLeft->getColor(), game->hasUncommitedPoints(GameSide::a));
        ledDisplay.setIndicatorAppearancePlayerB(playerRight->getColor(), game->hasUncommitedPoints(GameSide::b));
        ledDisplay.setBorderAppearance(
            playerLeft->getColor(),
            playerRight->getColor(),
            game->hasUncommitedPoints(GameSide::a),
            game->hasUncommitedPoints(GameSide::b)
        );

        if (shouldUpdateLedBarState) {
            ledDisplay.setLedBarState([&] { return GameScoreHistoryBarRenderer::toLedBarPixels(
                playerLeft->getColor(),
                playerRight->getColor(),
                game->getScoreHistory()
            ); });

            shouldUpdateLedBarState = false;
        }

        // Game ball outranks on fire: that side's smoke goes off, its breathing runs unmasked.
        const bool breatheA = game->willWinOnNextPointScored(GameSide::a);
        const bool breatheB = game->willWinOnNextPointScored(GameSide::b);
        ledDisplay.setOnFire(LedDisplay::onFireTargets(
            game->isOnFire(GameSide::a) && !breatheA,
            game->isOnFire(GameSide::b) && !breatheB
        ));
        ledDisplay.setBreathing(LedDisplay::breathingTargets(breatheA, breatheB));

        // After setOnFire, so a burst started this frame is not overwritten by it;
        // GameSide::a is the left court player in this view.
        const GameSide comebackSide = game->takeComebackSide();
        if (comebackSide == GameSide::a) {
            ledDisplay.startComeback(playerLeft->getColor(), true);
        } else if (comebackSide == GameSide::b) {
            ledDisplay.startComeback(playerRight->getColor(), false);
        }

        ledDisplay.display();
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
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

        const MatchResult result = match->getMatchResult();
        einkDisplay.showMatchScore(
            playerLeft->getName(), result.scoreOf(playerLeft->getId()),
            playerRight->getName(), result.scoreOf(playerRight->getId()),
            Str::MATCH_SCORE_LABEL_SETS
        );
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.clear();
        backDisplay.renderScoreWidget(game->getTemporaryScore(GameSide::a), game->getTemporaryScore(GameSide::b));
        backDisplay.display();

        shouldRenderBack = false;
    }

    // Game ball on the committed score, as the breathing above.
    void describeForWatch(WatchState &state) const override {
        if (game == nullptr) {
            View::describeForWatch(state);
            return;
        }

        WatchState::Playing p = WatchSupport::playing(*match, *playerLeft, *playerRight);
        p.uncommitted = game->hasUncommitedPoints();
        p.gameBallLeft = game->willWinOnNextPointScored(GameSide::a);
        p.gameBallRight = game->willWinOnNextPointScored(GameSide::b);
        p.leftScore = game->getTemporaryScore(GameSide::a);
        p.rightScore = game->getTemporaryScore(GameSide::b);
        state.setPlaying(p);
    }
};

#endif //SQUASH_MODE__GAME_PLAYING_VIEW_H
