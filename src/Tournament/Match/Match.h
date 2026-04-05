#ifndef MATCH_H
#define MATCH_H

#include "UserProfile.h"
#include "Tournament/Game/Game.h"
#include "Tournament/Rules/Rules.h"
#include "Tournament/Game/GameResult.h"

/**
 * Two preselected players play against each other in many rounds.
 * Lives as long as the tournament does.
 */
class Match {
    uint8_t id;

    UserProfile &playerA;
    UserProfile &playerB;

    // TODO: Only one active game
    std::deque<Game> games;
    // TODO: Remember Game results
    // std::vector<GameResult> matchResults;
    size_t activeRoundId = 0;
    Game *activeRound = nullptr;
    Rules *rules;

public:
    Match(const uint8_t id, UserProfile &userAProfile, UserProfile &userBProfile, Rules *rules)
        : id(id), playerA(userAProfile), playerB(userBProfile), rules(rules) {
    }

    uint8_t getId() const {
        return id;
    }

    Game &createMatchRound() {
        const auto newRoundId = games.size();
        games.emplace_back(newRoundId, rules);

        printLn("Creating new round %u:%u", id, newRoundId);

        return games.back();
    }

    void setActiveRound(const Game &round) {
        for (size_t i = 0; i < games.size(); i++) {
            if (games.at(i).getId() == round.getId()) {
                activeRoundId = i;
                activeRound = &games.at(i);

                printLn("Active round is %u:%u", id, round.getId());
                return;
            }
        }

        printLn("Could not find round %u:%u", id, round.getId());
    }

    Game &getActiveRound() {
        if (activeRound == nullptr) {
            setActiveRound(createMatchRound());
        }

        return *activeRound;
    }

    Game &getRound(const size_t gameId) {
        return games.at(gameId);
    }

    size_t getRoundCount() const {
        return games.size();
    }

    UserProfile &getPlayerA() const {
        return playerA;
    }

    UserProfile &getPlayerB() const {
        return playerB;
    }
};

#endif //MATCH_H
