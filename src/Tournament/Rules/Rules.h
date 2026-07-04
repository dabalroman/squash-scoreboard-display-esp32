#ifndef RULES_H
#define RULES_H

#include <cstddef>

class Rules {
public:
    virtual ~Rules() {
    }

    virtual GameSide checkWinner(int8_t scoreA, int8_t scoreB) const = 0;

    /**
     * How many score-history entries to reserve for a game under these rules.
     * Sized per sport to avoid reallocation during play (points/gems + undo
     * headroom). Defaults to 32; sports with longer games override it.
     */
    virtual size_t historyReserve() const {
        return 32;
    }
};

#endif //RULES_H
