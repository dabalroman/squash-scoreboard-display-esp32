#ifndef RULES_H
#define RULES_H

class Rules {
public:
    virtual ~Rules() {
    }

    virtual GameSide checkWinner(int8_t scoreA, int8_t scoreB) const = 0;
};

#endif //RULES_H
