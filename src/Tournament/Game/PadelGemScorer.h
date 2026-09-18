#ifndef PADEL_GEM_SCORER_H
#define PADEL_GEM_SCORER_H

#include <Arduino.h>

#include "Tournament/GameSide.h"
#include "GameScoreHistory.h"

/**
 * Tennis-style point token shown for one side within the current gem.
 * Advantage is only ever reported for the side that leads during deuce.
 */
enum class PadelPoint : uint8_t {
    Love = 0,
    Fifteen = 1,
    Thirty = 2,
    Forty = 3,
    Advantage = 4,
};

/**
 * Scores a single padel gem using the advantage system (win by two points).
 *
 * The gem is backed by a rally-level GameScoreHistory — the very component a
 * Game uses one level up — so every rally is recorded and undoable exactly like
 * the other sports. Points are derived by counting entries per side: the gem is
 * won by the first side to reach 4 points with a 2-point lead, which naturally
 * yields deuce/advantage (3-3 = deuce, 4-3 = advantage, 5-3 = gem, 4-4 = deuce).
 *
 * A rally is uncommitted (undoable) until commit(), so the 4-second undo window
 * behaves exactly like the other sports. commit() returns the gem winner when
 * the committed rallies complete the gem, otherwise none. It does NOT reset the
 * gem — the owner snapshots the finished gem's history (so it can step back into
 * it later) and calls reset() to start the next gem.
 */
class PadelGemScorer {
    // A gem tops out around 8 rallies (standard) and stays well under 16 even in
    // a long advantage battle, so reserve 16 up front — the live gem never
    // reallocates in practice. An uncapped tiebreak runs longer (15-13 is 28
    // rallies), so it reserves twice as much.
    static constexpr size_t GEM_RALLY_RESERVE = 16;
    static constexpr size_t TIEBREAK_RALLY_RESERVE = 32;

    // Points that take a gem, win by 2: the ladder's 40 is the 4th point, a
    // tiebreak runs to 7.
    static constexpr uint8_t GEM_TARGET = 4;
    static constexpr uint8_t TIEBREAK_TARGET = 7;

    bool tiebreak = false;

    GameScoreHistory history = GameScoreHistory(GEM_RALLY_RESERVE);

    static GameSide opposite(const GameSide side) {
        return side == GameSide::a ? GameSide::b : GameSide::a;
    }

    // Points including tentative rallies (committed + scored, excluding lost).
    uint8_t temporaryPoints(const GameSide side) const {
        uint8_t points = 0;
        for (const GameScoreHistoryEntry &entry : history.getHistory()) {
            if (entry.side == side && entry.status != GameScoreHistoryStatus::lost) {
                points++;
            }
        }
        return points;
    }

    uint8_t committedPoints(const GameSide side) const {
        uint8_t points = 0;
        for (const GameScoreHistoryEntry &entry : history.getHistory()) {
            if (entry.side == side && entry.status == GameScoreHistoryStatus::committed) {
                points++;
            }
        }
        return points;
    }

    GameSide winnerOf(const int a, const int b) const {
        const int target = tiebreak ? TIEBREAK_TARGET : GEM_TARGET;

        if (a >= target && (a - b) >= 2) return GameSide::a;
        if (b >= target && (b - a) >= 2) return GameSide::b;
        return GameSide::none;
    }

public:
    /**
     * Tiebreak mode: the same rally history, scored numerically to 7 instead of
     * the 0/15/30/40 ladder's 4. The owner derives it from the gem score rather
     * than latching it, so stepping back out of a tiebreak restores the ladder.
     */
    void setTiebreak(const bool value) {
        tiebreak = value;
    }

    bool isTiebreak() const {
        return tiebreak;
    }

    void scoreRally(const GameSide side) {
        history.scorePoint(side);
    }

    void undoRally(const GameSide side) {
        history.losePoint(side);
    }

    bool hasUncommittedRallies(const GameSide side) const {
        for (const GameScoreHistoryEntry &entry : history.getHistory()) {
            if (entry.side == side && entry.status != GameScoreHistoryStatus::committed) {
                return true;
            }
        }
        return false;
    }

    bool hasUncommittedRallies() const {
        for (const GameScoreHistoryEntry &entry : history.getHistory()) {
            if (entry.status != GameScoreHistoryStatus::committed) {
                return true;
            }
        }
        return false;
    }

    bool isEmpty() const {
        return temporaryPoints(GameSide::a) == 0 && temporaryPoints(GameSide::b) == 0;
    }

    /**
     * Who would win the gem if the current tentative rallies were committed now.
     * Lets the UI capture the deciding point/side before commit().
     */
    GameSide pendingGemWinner() const {
        return winnerOf(temporaryPoints(GameSide::a), temporaryPoints(GameSide::b));
    }

    /**
     * Applies the uncommitted rallies. Returns the gem winner if the gem just
     * completed, otherwise none. Does not reset — snapshot then reset() the gem.
     */
    GameSide commit() {
        history.commit();
        return winnerOf(committedPoints(GameSide::a), committedPoints(GameSide::b));
    }

    /**
     * Display token for the given side, based on temporary (committed + tentative)
     * points so the front display updates immediately, blinking until commit.
     */
    PadelPoint getPoint(const GameSide side) const {
        const uint8_t self = temporaryPoints(side);
        const uint8_t other = temporaryPoints(opposite(side));

        if (self >= 3 && other >= 3) {
            return self > other ? PadelPoint::Advantage : PadelPoint::Forty;
        }

        if (self >= 3) return PadelPoint::Forty;
        if (self == 2) return PadelPoint::Thirty;
        if (self == 1) return PadelPoint::Fifteen;
        return PadelPoint::Love;
    }

    /** Raw point count (committed + tentative) — what a tiebreak shows in place of the ladder. */
    uint8_t getRawPoints(const GameSide side) const {
        return temporaryPoints(side);
    }

    /** Full rally history of the current gem, for snapshotting a finished gem. */
    const GameScoreHistory &scoreHistory() const {
        return history;
    }

    /** Restores a previously snapshotted gem so the user can step back into it. */
    void restore(const GameScoreHistory &saved) {
        history = saved;
    }

    /** Clears the gem for a fresh start, as a normal gem or as the tiebreak. */
    void reset(const bool asTiebreak = false) {
        tiebreak = asTiebreak;
        history = GameScoreHistory(asTiebreak ? TIEBREAK_RALLY_RESERVE : GEM_RALLY_RESERVE);
    }
};

#endif //PADEL_GEM_SCORER_H
