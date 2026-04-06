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

public:
    explicit GameScoreHistory() {
        history.reserve(30);
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
};

#endif //GAME_SCORE_HISTORY
