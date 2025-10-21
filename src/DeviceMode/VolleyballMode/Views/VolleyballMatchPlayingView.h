#ifndef VOLLEYBALL_MODE__MATCH_PLAYING_VIEW_H
#define VOLLEYBALL_MODE__MATCH_PLAYING_VIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/VolleyballMode/VolleyballModeState.h"
#include "Display/GlyphDisplay.h"
#include "Match/Tournament.h"

#define MATCH_PLAYING_VIEW_COMMIT_TIMEOUT_MS 4000

class VolleyballMatchPlayingView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    MatchRound *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr, *lastPointScoredBy = nullptr;
    std::function<void(VolleyballModeState)> onStateChange;

    uint32_t lastPointScoredAtMs = 0;
    MatchSide commitResultWinner = MatchSide::none;

public:
    VolleyballMatchPlayingView(Tournament &tournament, std::function<void(VolleyballModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = tournament.getActiveMatch();

        if (match == nullptr) {
            printLn("MATCH IS MISSING!");
            return;
        }

        printLn("Active match id: %u", match->getId());

        round = &match->createMatchRound();
        match->setActiveRound(*round);

        playerA = &match->getPlayerA();
        playerB = &match->getPlayerB();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        const uint32_t now = millis();
        bool checkExit = false;

        if (remoteInputManager.buttonA.takeActionIfPossible(1000)) {
            round->scorePoint(MatchSide::a);
            lastPointScoredBy = &match->getPlayerA();
            lastPointScoredAtMs = now;
        }

        if (remoteInputManager.buttonB.takeActionIfPossible(1000)) {
            round->scorePoint(MatchSide::b);
            lastPointScoredBy = &match->getPlayerB();
            lastPointScoredAtMs = now;
        }

        if (remoteInputManager.buttonC.takeActionIfPossible(1000)) {
            round->losePoint(MatchSide::a);
            checkExit = true;
            lastPointScoredAtMs = now;
        }

        if (remoteInputManager.buttonD.takeActionIfPossible(1000)) {
            round->losePoint(MatchSide::b);
            checkExit = true;
            lastPointScoredAtMs = now;
        }

        // If both players are at 0, return to previous screen
        if (checkExit && round->getTemporaryScore(MatchSide::a) == 0 && round->getTemporaryScore(MatchSide::b) == 0) {
            onStateChange(VolleyballModeState::MatchChoosePlayers);
            return;
        }

        // Handle score commits
        if (round->hasUncommitedPoints() && lastPointScoredAtMs + MATCH_PLAYING_VIEW_COMMIT_TIMEOUT_MS <= now) {
            commitResultWinner = round->commit();

            if (commitResultWinner != MatchSide::none) {
                remoteInputManager.preventTriggerForMs();
                onStateChange(VolleyballModeState::MatchOver);
            }
        }
    }

    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        if (lastPointScoredBy == nullptr) {
            glyphDisplay.setColonAppearance(Colors::White, true);
        } else {
            glyphDisplay.setColonAppearance(lastPointScoredBy->getColor(), true);
        }

        glyphDisplay.setNumericValue(round->getTemporaryScore(MatchSide::a), round->getTemporaryScore(MatchSide::b));
        glyphDisplay.setGlyphsAppearance(
            playerA->getColor(),
            playerB->getColor(),
            round->hasUncommitedPoints(MatchSide::a),
            round->hasUncommitedPoints(MatchSide::b)
        );

        glyphDisplay.setIndicatorAppearancePlayerA(playerA->getColor(), round->hasUncommitedPoints(MatchSide::a));
        glyphDisplay.setIndicatorAppearancePlayerB(playerB->getColor(), round->hasUncommitedPoints(MatchSide::b));

        glyphDisplay.display();
    }

    void renderScreen(BackDisplay &backDisplay) override {
        backDisplay.clear();
        backDisplay.renderScoreWidget(round->getTemporaryScore(MatchSide::a), round->getTemporaryScore(MatchSide::b));
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //VOLLEYBALL_MODE__MATCH_PLAYING_VIEW_H
