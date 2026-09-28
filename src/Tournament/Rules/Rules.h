#ifndef RULES_H
#define RULES_H

#include <cstddef>

#include "../GameSide.h"

class Rules {
public:
    virtual ~Rules() {
    }

    virtual GameSide checkWinner(int8_t scoreA, int8_t scoreB) const = 0;

    /**
     * Game ball: `side` wins on its next point. Per side rather than a returned
     * GameSide, so a rule set that allows both at once never forces a pick.
     * An already won score is nobody's game ball.
     */
    virtual bool willWinOnNextPointScored(const int8_t scoreA, const int8_t scoreB, const GameSide side) const {
        if (side == GameSide::none || checkWinner(scoreA, scoreB) != GameSide::none) return false;

        return side == GameSide::a
                   ? checkWinner(scoreA + 1, scoreB) == GameSide::a
                   : checkWinner(scoreA, scoreB + 1) == GameSide::b;
    }

    /**
     * How many score-history entries to reserve for a game under these rules.
     * Sized per sport to avoid reallocation during play (points/gems + undo
     * headroom). Defaults to 32; sports with longer games override it.
     */
    virtual size_t historyReserve() const {
        return 32;
    }

    /**
     * Trailing same-side streak of committed history entries that lights the
     * "on fire" smoke (LedSmokeAnimation). Breaking an opponent's streak of
     * this length is also what fires the comeback burst. Padel counts gems.
     */
    virtual uint8_t onFireStreak() const {
        return 5;
    }
};

#endif //RULES_H
