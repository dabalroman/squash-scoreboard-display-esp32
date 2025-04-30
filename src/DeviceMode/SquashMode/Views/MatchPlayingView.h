#ifndef MATCH_PLAYING_VIEW_H
#define MATCH_PLAYING_VIEW_H

#include "Adafruit_SSD1306.h"
#include "RemoteInputManager.h"
#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/GlyphDisplay.h"
#include "Match/Tournament.h"

class MatchPlayingView final : public View {
    Tournament &tournament;
    Match *match = nullptr;
    MatchRound *round = nullptr;
    UserProfile *playerA = nullptr, *playerB = nullptr;
    std::function<void(SquashModeState)> onStateChange;

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
        if (remoteInputManager.buttonA.takeActionIfPossible()) {
            round->scorePoint(MatchSide::a);
            lastPointScoredAtMs = millis();
        }

        if (remoteInputManager.buttonB.takeActionIfPossible()) {
            round->scorePoint(MatchSide::b);
            lastPointScoredAtMs = millis();
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            round->losePoint(MatchSide::a);
            lastPointScoredAtMs = millis();
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            round->losePoint(MatchSide::b);
            lastPointScoredAtMs = millis();
        }

        if (lastPointScoredAtMs + 5000 <= millis()) {
            commitResultWinner = round->commit();
        }

        if (commitResultWinner != MatchSide::none) {
            if (commitResultWinner == MatchSide::a) {
                playerA->scorePoint();
            } else if (commitResultWinner == MatchSide::b) {
                playerB->scorePoint();
            }

            onStateChange(SquashModeState::MatchOver);
        }
    }

    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        glyphDisplay.clear();
        glyphDisplay.setColonState(true);
        glyphDisplay.setColonBlinking(true);
        glyphDisplay.setValue(round->getTemporaryScore(MatchSide::a), round->getTemporaryScore(MatchSide::b));
        glyphDisplay.setGlyphColor(playerA->getColor(), playerB->getColor());
        glyphDisplay.setGlyphBlinking(
            round->hasUncommitedPoints(MatchSide::a),
            round->hasUncommitedPoints(MatchSide::b)
        );
        glyphDisplay.render();
        glyphDisplay.show();
    }

    void renderBack(Adafruit_SSD1306 &backDisplay) override {
    }
};

#endif //MATCH_PLAYING_VIEW_H
