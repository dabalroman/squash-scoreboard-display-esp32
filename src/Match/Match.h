#ifndef MATCH_H
#define MATCH_H

#include <vector>

#include "UserProfile.h"
#include "MatchRound.h"
#include "Rules/Rules.h"

class Match {
    uint8_t id;

    UserProfile &userAProfile;
    UserProfile &userBProfile;

    std::vector<MatchRound> rounds;
    size_t activeRoundId = 0;
    MatchRound *activeRound = nullptr;
    Rules &rules;
public:
    Match(const uint8_t id, UserProfile &userAProfile, UserProfile &userBProfile, Rules &rules)
        : id(id), userAProfile(userAProfile), userBProfile(userBProfile), rules(rules) {
    }

    uint8_t getId() const {
        return id;
    }

    MatchRound& createMatchRound() {
        const auto newRoundId = rounds.size();
        rounds.emplace_back(newRoundId, rules);
        setActiveRound(newRoundId);
        return rounds.back();
    }

    void setActiveRound(const size_t roundId) {
        activeRoundId = roundId;
        activeRound = &rounds.at(roundId);
    }

    MatchRound& getActiveRound() {
        if (activeRound == nullptr) {
            createMatchRound();
        }

        return *activeRound;
    }

    MatchRound& getRound(const size_t id) {
        return rounds.at(id);
    }

    size_t getRoundCount() const {
        return rounds.size();
    }

    UserProfile &getUserAProfile() const {
        return userAProfile;
    }

    UserProfile &getUserBProfile() const {
        return userBProfile;
    }
};

#endif //MATCH_H
