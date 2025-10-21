#ifndef MATCH_H
#define MATCH_H

#include "UserProfile.h"
#include "MatchRound.h"
#include "Rules/Rules.h"

class Match {
    uint8_t id;

    UserProfile &playerA;
    UserProfile &playerB;

    std::deque<MatchRound> rounds;
    size_t activeRoundId = 0;
    MatchRound *activeRound = nullptr;
    Rules *rules;

public:
    Match(const uint8_t id, UserProfile &userAProfile, UserProfile &userBProfile, Rules *rules)
        : id(id), playerA(userAProfile), playerB(userBProfile), rules(rules) {
    }

    uint8_t getId() const {
        return id;
    }

    MatchRound &createMatchRound() {
        const auto newRoundId = rounds.size();
        rounds.emplace_back(newRoundId, rules);

        printLn("Creating new round %u:%u", id, newRoundId);

        return rounds.back();
    }

    void setActiveRound(const MatchRound &round) {
        for (size_t i = 0; i < rounds.size(); i++) {
            if (rounds.at(i).getId() == round.getId()) {
                activeRoundId = i;
                activeRound = &rounds.at(i);

                printLn("Active round is %u:%u", id, round.getId());
                return;
            }
        }

        printLn("Could not find round %u:%u", id, round.getId());
    }

    MatchRound &getActiveRound() {
        if (activeRound == nullptr) {
            setActiveRound(createMatchRound());
        }

        return *activeRound;
    }

    MatchRound &getRound(const size_t id) {
        return rounds.at(id);
    }

    size_t getRoundCount() const {
        return rounds.size();
    }

    UserProfile &getPlayerA() const {
        return playerA;
    }

    UserProfile &getPlayerB() const {
        return playerB;
    }
};

#endif //MATCH_H
