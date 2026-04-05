#ifndef TOURNAMENT_H
#define TOURNAMENT_H
#include <vector>

#include "Match/Match.h"
#include "Match/MatchOrderKeeper.h"

/**
 * Multiple players play against each other in many rounds.
 * Lives as long as the selected device mode.
 */
class Tournament {
    std::deque<Match> matches;
    std::vector<UserProfile *> players;
    std::unique_ptr<Rules> rules;
    Match *activeMatch = nullptr;
    size_t activeMatchId = 0;

public:
    std::unique_ptr<MatchOrderKeeper> matchOrderKeeper;

    explicit Tournament(std::unique_ptr<Rules> r) : rules(std::move(r)),
                                                    matchOrderKeeper(std::make_unique<MatchOrderKeeper>()) {
    }

    Match &createMatch(UserProfile &userProfileA, UserProfile &userProfileB) {
        printLn("Creating new match");
        const auto newMatchId = matches.size();
        matches.emplace_back(newMatchId, userProfileA, userProfileB, rules.get());
        return matches.back();
    }

    void addPlayer(UserProfile &userProfile) {
        if (isPlayerIn(userProfile)) {
            return;
        }

        players.push_back(&userProfile);
        matchOrderKeeper->setPlayers(players);
    }

    void removePlayer(UserProfile &userProfile) {
        if (!isPlayerIn(userProfile)) {
            return;
        }

        players.erase(std::remove(players.begin(), players.end(), &userProfile), players.end());
        matchOrderKeeper->setPlayers(players);
    }

    bool isPlayerIn(const UserProfile &userProfile) {
        return std::find(players.begin(), players.end(), &userProfile) != players.end();
    }

    std::vector<UserProfile *> &getPlayers() {
        return players;
    }

    bool setActiveMatch(const Match &match) {
        for (size_t i = 0; i < matches.size(); i++) {
            if (matches.at(i).getId() == match.getId()) {
                activeMatchId = match.getId();
                activeMatch = &matches.at(i);
                return true;
            }
        }

        printLn("Could not find match with id %d", match.getId());

        return false;
    }

    Match *getActiveMatch() const {
        return activeMatch;
    }

    Match &getMatchBetween(UserProfile &userProfileA, UserProfile &userProfileB) {
        for (auto &match: matches) {
            auto &matchPlayerA = match.getPlayerA();
            auto &matchPlayerB = match.getPlayerB();

            if (
                (matchPlayerA.getId() == userProfileA.getId() && matchPlayerB.getId() == userProfileB.getId())
                || (matchPlayerA.getId() == userProfileB.getId() && matchPlayerB.getId() == userProfileA.getId())
            ) {
                printLn("Match between %u and %u found as %u", userProfileA.getId(), userProfileB.getId(), match.getId());
                return match;
            }
        }

        return createMatch(userProfileA, userProfileB);
    }

    size_t getMatchCount() const {
        return matches.size();
    }
};

#endif //TOURNAMENT_H
