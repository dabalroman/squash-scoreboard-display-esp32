#ifndef RULES_H
#define RULES_H

#include "../MatchSideEnum.h"

class Rules {
public:
    virtual ~Rules() {
    }

    virtual MatchSide checkWinner(int8_t scoreA, int8_t scoreB) const = 0;
};

#endif //RULES_H
