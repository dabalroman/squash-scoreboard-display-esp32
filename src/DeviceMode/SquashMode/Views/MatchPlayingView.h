#ifndef MATCH_PLAYING_VIEW_H
#define MATCH_PLAYING_VIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/GlyphDisplay.h"
#include "Match/Tournament.h"

#define MATCH_PLAYING_VIEW_COMMIT_TIMEOUT_MS 5000
#define MATCH_PLAYING_VIEW_BACK_DISPLAY_PLAYER_CHANGE_MS 2500

class MatchPlayingView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    MatchRound *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr;
    std::function<void(SquashModeState)> onStateChange;

    uint32_t lastBackDisplayPlayerChangeMs = 0;
    MatchSide lastBackDisplayPlayerSide = MatchSide::a;

    uint32_t lastPointScoredAtMs = 0;
    MatchSide commitResultWinner = MatchSide::none;

public:
    MatchPlayingView(Tournament &tournament, std::function<void(SquashModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = &tournament.getActiveMatch();
        round = &match->createMatchRound();
        match->setActiveRound(*round);

        playerA = &match->getPlayerA();
        playerB = &match->getPlayerB();
    };

    void handleInput(RemoteInputManager &remoteInputManager) override {
        const uint32_t now = millis();

        if (remoteInputManager.buttonA.takeActionIfPossible()) {
            round->scorePoint(MatchSide::a);
            lastPointScoredAtMs = now;
        }

        if (remoteInputManager.buttonB.takeActionIfPossible()) {
            round->scorePoint(MatchSide::b);
            lastPointScoredAtMs = now;
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            round->losePoint(MatchSide::a);
            lastPointScoredAtMs = now;
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            round->losePoint(MatchSide::b);
            lastPointScoredAtMs = now;
        }

        // Handle back display player cycle
        if (!round->hasUncommitedPoints() && lastBackDisplayPlayerChangeMs + MATCH_PLAYING_VIEW_BACK_DISPLAY_PLAYER_CHANGE_MS <= now) {
            lastBackDisplayPlayerChangeMs = millis();
            lastBackDisplayPlayerSide = lastBackDisplayPlayerSide == MatchSide::a ? MatchSide::b : MatchSide::a;
        }

        // Handle score commits
        if (round->hasUncommitedPoints() && lastPointScoredAtMs + MATCH_PLAYING_VIEW_COMMIT_TIMEOUT_MS <= now) {
            lastBackDisplayPlayerChangeMs = millis();
            lastBackDisplayPlayerSide = round->hasUncommitedPoints(MatchSide::a) ? MatchSide::a : MatchSide::b;
            commitResultWinner = round->commit();

            if (commitResultWinner != MatchSide::none) {
                remoteInputManager.preventTriggerForMs();
                onStateChange(SquashModeState::MatchOver);
            }
        }
    }

    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        glyphDisplay.setColonAppearance(Colors::White, true);

        glyphDisplay.setNumericValue(round->getTemporaryScore(MatchSide::a), round->getTemporaryScore(MatchSide::b));
        glyphDisplay.setGlyphsAppearance(
            playerA->getColor(),
            playerB->getColor(),
            round->hasUncommitedPoints(MatchSide::a),
            round->hasUncommitedPoints(MatchSide::b)
        );

        if (round->hasUncommitedPoints()) {
            // Uncommited points, blink the player that just scored
            if (round->hasUncommitedPoints(MatchSide::a)) {
                glyphDisplay.setPlayerAIndicatorAppearance(playerA->getColor(), true);
                glyphDisplay.setPlayerBIndicatorAppearance(Colors::Black);
            } else {
                glyphDisplay.setPlayerAIndicatorAppearance(Colors::Black);
                glyphDisplay.setPlayerBIndicatorAppearance(playerB->getColor(), true);
            }
        } else {
            // Standard display, cycle between players
            if (lastBackDisplayPlayerSide == MatchSide::a) {
                glyphDisplay.setPlayerAIndicatorAppearance(playerA->getColor());
                glyphDisplay.setPlayerBIndicatorAppearance(Colors::Black);
            } else {
                glyphDisplay.setPlayerAIndicatorAppearance(Colors::Black);
                glyphDisplay.setPlayerBIndicatorAppearance(playerB->getColor());
            }
        }

        glyphDisplay.display();
    }

    void renderScreen(BackDisplay &backDisplay) override {
        backDisplay.clear();

        if (round->hasUncommitedPoints(MatchSide::a)) {
            backDisplay.setBlinking(true);
            backDisplay.renderScore(round->getTemporaryScore(MatchSide::a));
        } else if (round->hasUncommitedPoints(MatchSide::b)) {
            backDisplay.setBlinking(true);
            backDisplay.renderScore(round->getTemporaryScore(MatchSide::b));
        } else {
            backDisplay.setBlinking(false);
            backDisplay.renderScore(round->getTemporaryScore(lastBackDisplayPlayerSide));
        }

        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //MATCH_PLAYING_VIEW_H
