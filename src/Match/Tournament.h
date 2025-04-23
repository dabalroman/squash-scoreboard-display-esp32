#ifndef TOURNAMENT_H
#define TOURNAMENT_H
#include <vector>

#include "Match.h"

class Tournament {
    std::vector<Match> matches;
    Match *activeMatch = nullptr;
    size_t activeMatchId = 0;
    Rules &rules;

public:
    explicit Tournament(Rules &rules) : rules(rules) {
    }

    Match &createMatch(UserProfile &userProfileA, UserProfile &userProfileB) {
        const auto newMatchId = matches.size();
        matches.emplace_back(newMatchId, userProfileA, userProfileB, rules);
        return matches.back();
    }

    void setActiveMatch(const size_t matchId) {
        activeMatchId = matchId;
        activeMatch = &matches.at(matchId);
    }

    void setActiveMatch(Match &match) {
        activeMatchId = match.getId();
        activeMatch = &match;
    }

    Match &getActiveMatch() const {
        return *activeMatch;
    }

    Match &getMatch(const size_t id) {
        return matches.at(id);
    }

    Match &getMatchBetween(UserProfile &userProfileA, UserProfile &userProfileB) {
        for (uint8_t i = 0; i < matches.size(); i++) {
            if (
                matches.at(i).getUserAProfile().getId() == userProfileA.getId()
                && matches.at(i).getUserBProfile().getId() == userProfileB.getId()
            ) {
                return matches.at(i);
            }
        }

        return createMatch(userProfileA, userProfileB);
    }

    size_t getMatchCount() const {
        return matches.size();
    }
};

#endif //TOURNAMENT_H
