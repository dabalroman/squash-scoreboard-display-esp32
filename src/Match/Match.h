#ifndef MATCH_H
#define MATCH_H

#include <vector>

#include "UserProfile.h"
#include "MatchRound.h"
#include "Rules/Rules.h"

class Match {
    UserProfile &userAProfile;
    UserProfile &userBProfile;

    std::vector<MatchRound> rounds;
    size_t activeRoundId = 0;
    MatchRound *activeRound = nullptr;
    Rules &rules;
public:
    Match(UserProfile &userAProfile, UserProfile &userBProfile, Rules &rules)
        : userAProfile(userAProfile), userBProfile(userBProfile), rules(rules) {
    }

    void setActiveRound(const size_t roundId) {
        activeRoundId = roundId;
        activeRound = &rounds.at(roundId);
    }

    void scorePointA() const {
        activeRound->scorePointA();
    }

    void scorePointB() const {
        activeRound->scorePointA();
    }

    MatchRound& createMatchRound() {
        rounds.emplace_back(rounds.size());
        return rounds.back();
    }

    MatchRound& getRound(const size_t id) {
        return rounds.at(id);
    }

    size_t getRoundCount() const {
        return rounds.size();
    }
};

#endif //MATCH_H
