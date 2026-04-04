#ifndef MATCH_HISTORY
#define MATCH_HISTORY

#include <vector>
#include "Enum/MatchScoreHistoryStatus.h"
#include "Enum/MatchSide.h"

struct MatchScoreHistoryEntry {
    MatchSide side;
    MatchScoreHistoryStatus status;
};

class MatchScoreHistory {
    std::vector<MatchScoreHistoryEntry> history;

public:
    explicit MatchScoreHistory() {
        history.reserve(30);
    }

    const std::vector<MatchScoreHistoryEntry> &getHistory() const {
        return history;
    }

    void losePoint(const MatchSide side) {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->side == side && it->status == MatchScoreHistoryStatus::scored) {
                history.erase(std::next(it).base());
                return;
            }
        }

        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->side == side && it->status == MatchScoreHistoryStatus::committed) {
                it->status = MatchScoreHistoryStatus::lost;
                return;
            }
        }
    }

    void scorePoint(const MatchSide side) {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->side == side && it->status == MatchScoreHistoryStatus::lost) {
                it->status = MatchScoreHistoryStatus::committed;
                return;
            }
        }

        history.push_back({side, MatchScoreHistoryStatus::scored});
    }

    void commit() {
        for (auto it = history.begin(); it != history.end();) {
            if (it->status == MatchScoreHistoryStatus::lost) {
                it = history.erase(it);
            } else {
                it->status = MatchScoreHistoryStatus::committed;
                ++it;
            }
        }
    }
};

#endif //MATCH_HISTORY
