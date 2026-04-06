#ifndef VOLLEYBALL_MODE__GAME_PLAYING_VIEW_H
#define VOLLEYBALL_MODE__GAME_PLAYING_VIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/VolleyballMode/VolleyballModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "../../../Display/LedDisplay/Adapter/GameScoreHistoryBarAdapter.h"
#include "Tournament/Tournament.h"

class VolleyballGamePlayingView final : public View {
    constexpr static uint32_t COMMIT_TIMEOUT_MS = 4000;

    Tournament &tournament;
    Match *match = nullptr;
    Game *game = nullptr;
    UserProfile *playerLeft = nullptr, *playerRight = nullptr, *lastPointScoredBy = nullptr;
    std::function<void(VolleyballModeState)> onStateChange;

    uint32_t lastPointScoredAtMs = 0;
    GameSide commitResultWinner = GameSide::none;

    bool shouldUpdateLedBarState = true;

public:
    VolleyballGamePlayingView(Tournament &tournament, std::function<void(VolleyballModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = tournament.getActiveMatch();

        if (match == nullptr) {
            onStateChange(VolleyballModeState::MatchStartGame);
            return;
        }

        game = match->createGame();
        playerLeft = &match->getPlayerA();
        playerRight = &match->getPlayerB();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        const uint32_t now = millis();
        bool checkExit = false;

        if (remoteInputManager.buttonA.takeActionIfPossible(750)) {
            game->scorePoint(GameSide::a);
            lastPointScoredBy = &match->getPlayerA();
            lastPointScoredAtMs = now;
            shouldUpdateLedBarState = true;
        }

        if (remoteInputManager.buttonB.takeActionIfPossible(750)) {
            game->scorePoint(GameSide::b);
            lastPointScoredBy = &match->getPlayerB();
            lastPointScoredAtMs = now;
            shouldUpdateLedBarState = true;
        }

        if (remoteInputManager.buttonC.takeActionIfPossible(750)) {
            if (game->getTemporaryScore(GameSide::a) == 0 && game->getTemporaryScore(GameSide::b) == 0) {
                checkExit = true;
            }

            game->losePoint(GameSide::a);
            lastPointScoredAtMs = now;
            shouldUpdateLedBarState = true;
        }

        if (remoteInputManager.buttonD.takeActionIfPossible(750)) {
            if (game->getTemporaryScore(GameSide::a) == 0 && game->getTemporaryScore(GameSide::b) == 0) {
                checkExit = true;
            }

            game->losePoint(GameSide::b);
            lastPointScoredAtMs = now;
            shouldUpdateLedBarState = true;
        }

        if (checkExit) {
            onStateChange(VolleyballModeState::MatchStartGame);
            return;
        }

        // Handle score commits
        if (game->hasUncommitedPoints() && lastPointScoredAtMs + COMMIT_TIMEOUT_MS <= now) {
            commitResultWinner = game->commit();
            shouldUpdateLedBarState = true;

            if (commitResultWinner != GameSide::none) {
                remoteInputManager.preventTriggerForMs();
                match->finishGame();
                onStateChange(VolleyballModeState::GameOver);
            }
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.resetHistoryBar();
        ledDisplay.setPlayersIndicatorsState(true);
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

        if (shouldUpdateLedBarState) {
            ledDisplay.setLedBarState(GameScoreHistoryBarAdapter::toLedBarPixels(
                playerLeft->getColor(),
                playerRight->getColor(),
                game->getScoreHistory()
            ));

            shouldUpdateLedBarState = false;
        }

        ledDisplay.display();
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.initBigFont();
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.clear();
        backDisplay.renderScoreWidget(game->getTemporaryScore(GameSide::a), game->getTemporaryScore(GameSide::b));
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //VOLLEYBALL_MODE__GAME_PLAYING_VIEW_H
