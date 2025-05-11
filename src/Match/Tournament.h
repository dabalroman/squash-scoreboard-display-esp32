#ifndef TOURNAMENT_H
#define TOURNAMENT_H
#include <vector>

#include "Match.h"
#include "MatchOrderKeeper.h"

class Tournament {
    std::vector<Match> matches;
    std::vector<UserProfile*> players;
    Match *activeMatch = nullptr;
    size_t activeMatchId = 0;
    Rules &rules;

public:
    MatchOrderKeeper *matchOrderKeeper;

    explicit Tournament(Rules &rules) : rules(rules), matchOrderKeeper(new MatchOrderKeeper()) {
    }

    Match &createMatch(UserProfile &userProfileA, UserProfile &userProfileB) {
        printLn("Creating new match");
        const auto newMatchId = matches.size();
        matches.emplace_back(newMatchId, userProfileA, userProfileB, rules);
        return matches.back();
    }

    void addPlayer(UserProfile &userProfile) {
        players.push_back(&userProfile);
        matchOrderKeeper->addPlayers(players);
    }

    void removePlayer(UserProfile &userProfile) {
        players.erase(std::remove(players.begin(), players.end(), &userProfile), players.end());
        matchOrderKeeper->addPlayers(players);
    }

    bool isPlayerIn(const UserProfile &userProfile) {
        return std::find(players.begin(), players.end(), &userProfile) != players.end();
    }

    std::vector<UserProfile*> &getPlayers() {
        return players;
    }

    void setActiveMatch(const Match &match) {
        for (size_t i = 0; i < matches.size(); i++) {
            if (matches.at(i).getId() == match.getId()) {
                activeMatchId = match.getId();
                activeMatch = &matches.at(i);
                return;
            }
        }

        printLn("Could not find match with id %d", match.getId());
    }

    Match &getActiveMatch() const {
        if (activeMatch == nullptr) {
            printLn("No active match");
        }

        return *activeMatch;
    }

    Match &getMatchBetween(UserProfile &userProfileA, UserProfile &userProfileB) {
        for (uint8_t i = 0; i < matches.size(); i++) {
            if (
                matches.at(i).getPlayerA().getId() == userProfileA.getId()
                && matches.at(i).getPlayerB().getId() == userProfileB.getId()
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
