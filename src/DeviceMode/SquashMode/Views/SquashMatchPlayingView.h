#ifndef SQUASH_MODE__MATCH_PLAYING_VIEW_H
#define SQUASH_MODE__MATCH_PLAYING_VIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/ScoreHistoryBarAdapter.h"
#include "Match/Tournament.h"

#define MATCH_PLAYING_VIEW_COMMIT_TIMEOUT_MS 4000

class SquashMatchPlayingView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    MatchRound *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr, *lastPointScoredBy = nullptr;
    std::function<void(SquashModeState)> onStateChange;

    uint32_t lastPointScoredAtMs = 0;
    MatchSide commitResultWinner = MatchSide::none;

    bool shouldUpdateLedBarHistoryState = true;

public:
    SquashMatchPlayingView(Tournament &tournament, std::function<void(SquashModeState)> onStateChange)
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
            round->scorePoint(MatchSide::a);
            lastPointScoredBy = &match->getPlayerA();
            lastPointScoredAtMs = now;
            shouldUpdateLedBarHistoryState = true;
        }

        if (remoteInputManager.buttonB.takeActionIfPossible(750)) {
            round->scorePoint(MatchSide::b);
            lastPointScoredBy = &match->getPlayerB();
            lastPointScoredAtMs = now;
            shouldUpdateLedBarHistoryState = true;
        }

        if (remoteInputManager.buttonC.takeActionIfPossible(750)) {
            round->losePoint(MatchSide::a);
            checkExit = true;
            lastPointScoredAtMs = now;
            shouldUpdateLedBarHistoryState = true;
        }

        if (remoteInputManager.buttonD.takeActionIfPossible(750)) {
            round->losePoint(MatchSide::b);
            checkExit = true;
            lastPointScoredAtMs = now;
            shouldUpdateLedBarHistoryState = true;
        }

        // If both players are at 0, return to previous screen
        if (checkExit && round->getTemporaryScore(MatchSide::a) == 0 && round->getTemporaryScore(MatchSide::b) == 0) {
            onStateChange(SquashModeState::MatchChoosePlayers);
            return;
        }

        // Handle score commits
        if (round->hasUncommitedPoints() && lastPointScoredAtMs + MATCH_PLAYING_VIEW_COMMIT_TIMEOUT_MS <= now) {
            commitResultWinner = round->commit();
            shouldUpdateLedBarHistoryState = true;

            if (commitResultWinner != MatchSide::none) {
                remoteInputManager.preventTriggerForMs();
                onStateChange(SquashModeState::MatchOver);
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

        ledDisplay.setNumericValue(round->getTemporaryScore(MatchSide::a), round->getTemporaryScore(MatchSide::b));
        ledDisplay.setGlyphsAppearance(
            playerA->getColor(),
            playerB->getColor(),
            round->hasUncommitedPoints(MatchSide::a),
            round->hasUncommitedPoints(MatchSide::b)
        );

        ledDisplay.setIndicatorAppearancePlayerA(playerA->getColor(), round->hasUncommitedPoints(MatchSide::a));
        ledDisplay.setIndicatorAppearancePlayerB(playerB->getColor(), round->hasUncommitedPoints(MatchSide::b));

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
        backDisplay.renderScoreWidget(round->getTemporaryScore(MatchSide::a), round->getTemporaryScore(MatchSide::b));
        backDisplay.display();
    }
};

#endif //SQUASH_MODE__MATCH_PLAYING_VIEW_H
