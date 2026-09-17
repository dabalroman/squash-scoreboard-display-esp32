#ifndef PADEL_MODE__GAME_PLAYING_VIEW_H
#define PADEL_MODE__GAME_PLAYING_VIEW_H

#include <vector>

#include "DeviceMode/View.h"
#include "DeviceMode/PadelMode/PadelModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Renderer/GameScoreHistoryBarRenderer.h"
#include "Tournament/Tournament.h"
#include "Tournament/Game/GameScoreHistory.h"
#include "Tournament/Game/PadelGemScorer.h"

/**
 * Padel set scoring.
 *
 * Buttons A/B win a rally for the left/right side; C/D undo. A rally is
 * tentative for COMMIT_TIMEOUT_MS (undoable) exactly like the other sports.
 * On commit the gem ladder advances; when a gem is won it is registered as one
 * "point" on the engine Game (whose score = gems won this set). When the gems
 * satisfy PadelRules (6, win by 2) the set is over.
 *
 * Front display & rear OLED: current gem points (Love/15/30/40, "Ad" for
 * advantage). LED bar: gems won this set. While the uncommitted rally is itself
 * the gem-winning point, the display alters to show only the winning side's
 * deciding point (40 / Ad) with the opponent blanked, e.g. [40:  ] / [Ad:  ],
 * so winning a gem reads differently from merely scoring or gaining advantage.
 */
class PadelGamePlayingView final : public View {
    constexpr static uint32_t COMMIT_TIMEOUT_MS = 4000;

    Tournament &tournament;
    Match *match = nullptr;
    Game *game = nullptr;
    PadelGemScorer scorer;
    UserProfile *playerLeft = nullptr, *playerRight = nullptr, *lastPointScoredBy = nullptr;
    std::function<void(PadelModeState)> onStateChange;

    uint32_t lastPointScoredAtMs = 0;

    bool shouldUpdateLedBarState = true;

    struct CompletedGem {
        GameScoreHistory history;
        GameSide winner;
    };

    std::vector<CompletedGem> completedGems;

    struct GlyphPair {
        Glyph high;
        Glyph low;
    };

    static GlyphPair pointToGlyphs(const PadelPoint point) {
        switch (point) {
            default:
            case PadelPoint::Love:      return {Glyph::Empty, Glyph::D0};
            case PadelPoint::Fifteen:   return {Glyph::D1, Glyph::D5};
            case PadelPoint::Thirty:    return {Glyph::D3, Glyph::D0};
            case PadelPoint::Forty:     return {Glyph::D4, Glyph::D0};
            case PadelPoint::Advantage: return {Glyph::A, Glyph::d};
        }
    }

    static String pointToString(const PadelPoint point) {
        switch (point) {
            default:
            case PadelPoint::Love:      return "0";
            case PadelPoint::Fifteen:   return "15";
            case PadelPoint::Thirty:    return "30";
            case PadelPoint::Forty:     return "40";
            case PadelPoint::Advantage: return "Ad";
        }
    }

    void stepBackToPreviousGem() {
        const CompletedGem last = completedGems.back();
        completedGems.pop_back();

        game->losePoint(last.winner);
        game->commit();

        scorer.restore(last.history);
        scorer.undoRally(last.winner);
        scorer.commit();
    }

    void undoOrStepBack(const GameSide side, bool &checkExit) {
        if (!scorer.isEmpty()) {
            scorer.undoRally(side);
        } else if (!completedGems.empty()) {
            stepBackToPreviousGem();
        } else {
            checkExit = true;
        }
    }

public:
    PadelGamePlayingView(Tournament &tournament, std::function<void(PadelModeState)> onStateChange)
        : tournament(tournament), onStateChange(std::move(onStateChange)) {
        match = tournament.getActiveMatch();

        if (match == nullptr) {
            onStateChange(PadelModeState::MatchStartGame);
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
            scorer.scoreRally(GameSide::a);
            lastPointScoredBy = &match->getLeftCourtSidePlayer();
            lastPointScoredAtMs = now;
            shouldUpdateLedBarState = true;
        }

        if (remoteInputManager.buttonB.takeActionIfPossible(750)) {
            scorer.scoreRally(GameSide::b);
            lastPointScoredBy = &match->getRightCourtSidePlayer();
            lastPointScoredAtMs = now;
            shouldUpdateLedBarState = true;
        }

        if (remoteInputManager.buttonC.takeActionIfPossible(750)) {
            undoOrStepBack(GameSide::a, checkExit);
            lastPointScoredAtMs = now;
            shouldUpdateLedBarState = true;
        }

        if (remoteInputManager.buttonD.takeActionIfPossible(750)) {
            undoOrStepBack(GameSide::b, checkExit);
            lastPointScoredAtMs = now;
            shouldUpdateLedBarState = true;
        }

        if (checkExit) {
            onStateChange(PadelModeState::MatchStartGame);
            return;
        }

        if (scorer.hasUncommittedRallies() && (now - lastPointScoredAtMs) >= COMMIT_TIMEOUT_MS) {
            const GameSide gemWinner = scorer.commit();
            shouldUpdateLedBarState = true;

            if (gemWinner != GameSide::none) {
                game->scorePoint(gemWinner);
                const GameSide setWinner = game->commit();

                if (setWinner != GameSide::none) {
                    remoteInputManager.preventTriggerForMs();
                    match->finishGame();
                    onStateChange(PadelModeState::GameOver);
                    return;
                }

                completedGems.push_back({scorer.scoreHistory(), gemWinner});
                scorer.reset();
            }
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.resetHistoryBar();
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

        // If the uncommitted rally would win the gem, show only the winning
        // side's deciding point (40 / Ad), blinking, with the opponent blanked.
        const GameSide pendingWinner = scorer.pendingGemWinner();

        if (pendingWinner != GameSide::none) {
            const bool leftWon = pendingWinner == GameSide::a;
            const Color winnerColor = leftWon ? playerLeft->getColor() : playerRight->getColor();
            const GlyphPair point = pointToGlyphs(scorer.getPoint(pendingWinner));

            if (leftWon) {
                ledDisplay.setGlyphsGlyph(point.high, point.low, Glyph::Empty, Glyph::Empty);
            } else {
                ledDisplay.setGlyphsGlyph(Glyph::Empty, Glyph::Empty, point.high, point.low);
            }

            ledDisplay.setGlyphsColor(winnerColor, winnerColor);
            ledDisplay.setGlyphBlinking(leftWon, !leftWon);
            ledDisplay.setIndicatorAppearancePlayerA(leftWon ? winnerColor : Colors::Black, leftWon);
            ledDisplay.setIndicatorAppearancePlayerB(!leftWon ? winnerColor : Colors::Black, !leftWon);
            ledDisplay.setBorderAppearance(
                leftWon ? winnerColor : Colors::Black,
                !leftWon ? winnerColor : Colors::Black,
                leftWon,
                !leftWon
            );
        } else {
            const GlyphPair left = pointToGlyphs(scorer.getPoint(GameSide::a));
            const GlyphPair right = pointToGlyphs(scorer.getPoint(GameSide::b));
            ledDisplay.setGlyphsGlyph(left.high, left.low, right.high, right.low);

            ledDisplay.setGlyphsAppearance(
                playerLeft->getColor(),
                playerRight->getColor(),
                scorer.hasUncommittedRallies(GameSide::a),
                scorer.hasUncommittedRallies(GameSide::b)
            );

            ledDisplay.setIndicatorAppearancePlayerA(playerLeft->getColor(), scorer.hasUncommittedRallies(GameSide::a));
            ledDisplay.setIndicatorAppearancePlayerB(playerRight->getColor(), scorer.hasUncommittedRallies(GameSide::b));
            ledDisplay.setBorderAppearance(
                playerLeft->getColor(),
                playerRight->getColor(),
                scorer.hasUncommittedRallies(GameSide::a),
                scorer.hasUncommittedRallies(GameSide::b)
            );
        }

        if (shouldUpdateLedBarState) {
            ledDisplay.setLedBarState([&] { return GameScoreHistoryBarRenderer::toLedBarPixels(
                playerLeft->getColor(),
                playerRight->getColor(),
                game->getScoreHistory(),
                1,
                2
            ); });

            shouldUpdateLedBarState = false;
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

        if (match == nullptr || game == nullptr || playerLeft == nullptr || playerRight == nullptr) {
            einkDisplay.showBlank();
            return;
        }

        // The engine Game is the set: its score is gems won. Side a = left court.
        const MatchResult sets = match->getMatchResult();
        einkDisplay.showMatchScore(
            playerLeft->getName(), game->getRealScore(GameSide::a),
            playerRight->getName(), game->getRealScore(GameSide::b),
            "GEMS",
            sets.scoreOf(playerLeft->getId()), sets.scoreOf(playerRight->getId())
        );
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.clear();

        const GameSide pendingWinner = scorer.pendingGemWinner();

        if (pendingWinner != GameSide::none) {
            const String winningPoint = pointToString(scorer.getPoint(pendingWinner));
            backDisplay.renderScoreWidget(
                pendingWinner == GameSide::a ? winningPoint : String(""),
                pendingWinner == GameSide::b ? winningPoint : String("")
            );
        } else {
            backDisplay.renderScoreWidget(
                pointToString(scorer.getPoint(GameSide::a)),
                pointToString(scorer.getPoint(GameSide::b))
            );
        }

        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //PADEL_MODE__GAME_PLAYING_VIEW_H
