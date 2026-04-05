#ifndef SQUASH_MODE__GAME_PLAYING_VIEW_H
#define SQUASH_MODE__GAME_PLAYING_VIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/ScoreHistoryBarAdapter.h"
#include "Tournament/Tournament.h"

#define GAME_PLAYING_VIEW_COMMIT_TIMEOUT_MS 4000

class SquashGamePlayingView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    Game *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr, *lastPointScoredBy = nullptr;
    std::function<void(SquashModeState)> onStateChange;

    uint32_t lastPointScoredAtMs = 0;
    GameSide commitResultWinner = GameSide::none;

    bool shouldUpdateLedBarHistoryState = true;

public:
    SquashGamePlayingView(Tournament &tournament, std::function<void(SquashModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = tournament.getActiveMatch();

        if (match == nullptr) {
            printLn("MATCH IS MISSING!");
            return;
        }

        round = &match->createMatchRound();
        match->setActiveRound(*round);

        playerA = &match->getPlayerA();
        playerB = &match->getPlayerB();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        const uint32_t now = millis();
        bool checkExit = false;

        if (remoteInputManager.buttonA.takeActionIfPossible(750)) {
            round->scorePoint(GameSide::a);
            lastPointScoredBy = &match->getPlayerA();
            lastPointScoredAtMs = now;
            shouldUpdateLedBarHistoryState = true;
        }

        if (remoteInputManager.buttonB.takeActionIfPossible(750)) {
            round->scorePoint(GameSide::b);
            lastPointScoredBy = &match->getPlayerB();
            lastPointScoredAtMs = now;
            shouldUpdateLedBarHistoryState = true;
        }

        if (remoteInputManager.buttonC.takeActionIfPossible(750)) {
            if (round->getTemporaryScore(GameSide::a) == 0 && round->getTemporaryScore(GameSide::b) == 0) {
                checkExit = true;
            }

            round->losePoint(GameSide::a);
            lastPointScoredAtMs = now;
            shouldUpdateLedBarHistoryState = true;
        }

        if (remoteInputManager.buttonD.takeActionIfPossible(750)) {
            if (round->getTemporaryScore(GameSide::a) == 0 && round->getTemporaryScore(GameSide::b) == 0) {
                checkExit = true;
            }

            round->losePoint(GameSide::b);
            lastPointScoredAtMs = now;
            shouldUpdateLedBarHistoryState = true;
        }

        if (checkExit) {
            onStateChange(SquashModeState::MatchStartGame);
            return;
        }

        // Handle score commits
        if (round->hasUncommitedPoints() && lastPointScoredAtMs + GAME_PLAYING_VIEW_COMMIT_TIMEOUT_MS <= now) {
            commitResultWinner = round->commit();
            shouldUpdateLedBarHistoryState = true;

            if (commitResultWinner != GameSide::none) {
                remoteInputManager.preventTriggerForMs();
                onStateChange(SquashModeState::GameOver);
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

        ledDisplay.setNumericValue(round->getTemporaryScore(GameSide::a), round->getTemporaryScore(GameSide::b));
        ledDisplay.setGlyphsAppearance(
            playerA->getColor(),
            playerB->getColor(),
            round->hasUncommitedPoints(GameSide::a),
            round->hasUncommitedPoints(GameSide::b)
        );

        ledDisplay.setIndicatorAppearancePlayerA(playerA->getColor(), round->hasUncommitedPoints(GameSide::a));
        ledDisplay.setIndicatorAppearancePlayerB(playerB->getColor(), round->hasUncommitedPoints(GameSide::b));

        if (shouldUpdateLedBarHistoryState) {
            ledDisplay.setHistoryBarState(ScoreHistoryBarAdapter::toLedBarPixels(
                playerA->getColor(),
                playerB->getColor(),
                round->getScoreHistory()
            ));

            shouldUpdateLedBarHistoryState = false;
        }

        ledDisplay.display();
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.initBigFont();
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.clear();
        backDisplay.renderScoreWidget(round->getTemporaryScore(GameSide::a), round->getTemporaryScore(GameSide::b));
        backDisplay.display();
    }
};

#endif //SQUASH_MODE__GAME_PLAYING_VIEW_H
