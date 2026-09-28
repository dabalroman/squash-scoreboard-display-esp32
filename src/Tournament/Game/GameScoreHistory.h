#ifndef GAME_SCORE_HISTORY
#define GAME_SCORE_HISTORY

#include <vector>
#include "GameScoreHistoryStatus.h"
#include "../GameSide.h"

struct GameScoreHistoryEntry {
    GameSide side;
    GameScoreHistoryStatus status;
};

class GameScoreHistory {
    std::vector<GameScoreHistoryEntry> history;
    size_t maxEntries;

public:
    // maxEntries=64 keeps Game (real int8_t counters, history only drives the
    // LED bar) bit-for-bit unchanged. PadelGemScorer derives its score BY
    // COUNTING entries, so an eviction there deletes a point someone won -
    // it must pass a cap its longest realistic history can never reach.
    explicit GameScoreHistory(const size_t reserveCount = 32, const size_t maxEntries = 64)
        : maxEntries(maxEntries) {
        history.reserve(reserveCount);
    }

    const std::vector<GameScoreHistoryEntry> &getHistory() const {
        return history;
    }

    void losePoint(const GameSide side) {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->side == side && it->status == GameScoreHistoryStatus::scored) {
                history.erase(std::next(it).base());
                return;
            }
        }

        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->side == side && it->status == GameScoreHistoryStatus::committed) {
                it->status = GameScoreHistoryStatus::lost;
                return;
            }
        }
    }

    void scorePoint(const GameSide side) {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->side == side && it->status == GameScoreHistoryStatus::lost) {
                it->status = GameScoreHistoryStatus::committed;
                return;
            }
        }

        if (history.size() >= maxEntries) {
            history.erase(history.begin());
        }

        history.push_back({side, GameScoreHistoryStatus::scored});
    }

    void commit() {
        for (auto it = history.begin(); it != history.end();) {
            if (it->status == GameScoreHistoryStatus::lost) {
                it = history.erase(it);
            } else {
                it->status = GameScoreHistoryStatus::committed;
                ++it;
            }
        }
    }

    /**
     * Trailing streak of one side's committed entries, walked from the most
     * recent backwards. `scored` (uncommitted) entries are skipped so an
     * opponent's not-yet-committed point can't break it early; `lost` counts
     * as committed (commit() is what actually erases it, mirroring
     * getRealScore()). Stops at the first non-`scored` entry of the other
     * side. Capped at 255 - every sport's threshold is a single digit.
     */
    uint8_t committedStreak(const GameSide side) const {
        uint16_t streak = 0;
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->status == GameScoreHistoryStatus::scored) continue;
            if (it->side != side) break;
            if (streak < 255) streak++;
        }
        return static_cast<uint8_t>(streak);
    }
};

#endif //GAME_SCORE_HISTORY
