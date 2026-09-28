#ifndef GAME_H
#define GAME_H

#include <Arduino.h>

#include "GameScoreHistory.h"
#include "Tournament/Rules/Rules.h"

/**
 * Preselected players play against each other one game
 * Short-lived, only for the duration of the actual game.
 */
class Game {
    int8_t scoreA = 0, scoreB = 0;
    int8_t deltaA = 0, deltaB = 0;
    GameSide winner = GameSide::none;
    GameSide comebackSide = GameSide::none;
    Rules *rules;
    GameScoreHistory history;

public:
    explicit Game(Rules *rules) : rules(rules), history(rules->historyReserve()) {
    }

    uint8_t getRealScore(const GameSide side) const {
        if (side == GameSide::a) {
            return scoreA;
        }

        if (side == GameSide::b) {
            return scoreB;
        }

        return 0;
    }

    uint8_t getTemporaryScore(const GameSide side) const {
        int8_t score = 0;

        if (side == GameSide::a) {
            score = scoreA + deltaA;
        } else if (side == GameSide::b) {
            score = scoreB + deltaB;
        }

        return static_cast<uint8_t>(score < 0 ? 0 : score);
    }

    /** Game ball on the committed score: an uncommitted point neither starts nor stops it. */
    bool willWinOnNextPointScored(const GameSide side) const {
        return rules->willWinOnNextPointScored(getRealScore(GameSide::a), getRealScore(GameSide::b), side);
    }

    /** On fire: `side`'s trailing committed streak has reached the sport's threshold. */
    bool isOnFire(const GameSide side) const {
        return history.committedStreak(side) >= rules->onFireStreak();
    }

    /**
     * One-shot comeback burst side from the last commit(), consumed on read.
     * Set there when a side's point breaks the opponent's on-fire streak,
     * cleared once taken or once a winner exists. Padel's view never takes it.
     */
    GameSide takeComebackSide() {
        const GameSide side = comebackSide;
        comebackSide = GameSide::none;
        return side;
    }

    GameSide getWinner() const {
        return winner;
    }

    void scorePoint(const GameSide side) {
        if (winner != GameSide::none) {
            return;
        }

        if (side == GameSide::a) {
            deltaA++;
            history.scorePoint(side);
        } else if (side == GameSide::b) {
            deltaB++;
            history.scorePoint(side);
        }
    }

    void losePoint(const GameSide side) {
        if (winner != GameSide::none) {
            return;
        }

        if (side == GameSide::a && scoreA + deltaA > 0) {
            deltaA--;
            history.losePoint(side);
        } else if (side == GameSide::b && scoreB + deltaB > 0) {
            deltaB--;
            history.losePoint(side);
        }
    }

    bool hasUncommitedPoints(const GameSide side) const {
        if (side == GameSide::a) {
            return deltaA != 0;
        }

        if (side == GameSide::b) {
            return deltaB != 0;
        }

        return false;
    }

    bool hasUncommitedPoints() const {
        return hasUncommitedPoints(GameSide::a) || hasUncommitedPoints(GameSide::b);
    }

    GameSide commit() {
        if (winner != GameSide::none) {
            return winner;
        }

        // Before history.commit(): the opponent's streak as it stood before this
        // batch (uncommitted entries are skipped). One side per commit.
        const uint8_t streak = rules->onFireStreak();
        if (deltaA > 0 && history.committedStreak(GameSide::b) >= streak) {
            comebackSide = GameSide::a;
        } else if (deltaB > 0 && history.committedStreak(GameSide::a) >= streak) {
            comebackSide = GameSide::b;
        } else {
            comebackSide = GameSide::none;
        }

        history.commit();

        scoreA += deltaA;
        scoreB += deltaB;

        deltaA = 0;
        deltaB = 0;

        scoreA = std::max<int8_t>(scoreA, 0);
        scoreB = std::max<int8_t>(scoreB, 0);

        winner = rules->checkWinner(scoreA, scoreB);
        if (winner != GameSide::none) {
            comebackSide = GameSide::none;   // the view hands over to GameCelebration anyway
        }
        return winner;
    }

    const GameScoreHistory &getScoreHistory() const {
        return history;
    }
};

#endif //GAME_H
