#ifndef SQUASH_MODE__MATCH_PLAYING_VIEW_H
#define SQUASH_MODE__MATCH_PLAYING_VIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/GlyphDisplay.h"
#include "Match/Tournament.h"

#define MATCH_PLAYING_VIEW_COMMIT_TIMEOUT_MS 4000

class SquashMatchPlayingView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    MatchRound *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr;
    std::function<void(SquashModeState)> onStateChange;

    uint32_t lastPointScoredAtMs = 0;
    MatchSide commitResultWinner = MatchSide::none;

public:
    SquashMatchPlayingView(Tournament &tournament, std::function<void(SquashModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = &tournament.getActiveMatch();
        round = &match->createMatchRound();
        match->setActiveRound(*round);

        playerA = &match->getPlayerA();
        playerB = &match->getPlayerB();
    }

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

        // Handle score commits
        if (round->hasUncommitedPoints() && lastPointScoredAtMs + MATCH_PLAYING_VIEW_COMMIT_TIMEOUT_MS <= now) {
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

#endif //SQUASH_MODE__MATCH_PLAYING_VIEW_H
