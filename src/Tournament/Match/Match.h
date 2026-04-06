#ifndef MATCH_H
#define MATCH_H

#include "MatchResult.h"
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

    std::unique_ptr<Game> game = nullptr;
    std::vector<GameResult> gameResults;

    Rules *rules;

    bool playersSwappedCourtSides = false;

public:
    Match(const uint8_t id, UserProfile &userAProfile, UserProfile &userBProfile, Rules *rules)
        : id(id), playerA(userAProfile), playerB(userBProfile), rules(rules) {
    }

    uint8_t getId() const {
        return id;
    }

    Game *createGame() {
        game = std::make_unique<Game>(rules);
        return game.get();
    }

    void finishGame() {
        auto sideA = GameSide::a;
        auto sideB = GameSide::b;

        if (playersSwappedCourtSides) {
            sideA = GameSide::b;
            sideB = GameSide::a;
        }

        const uint8_t winnerPlayerId = game->getWinner() == GameSide::a
            ? (playersSwappedCourtSides ? playerB.getId() : playerA.getId())
            : (playersSwappedCourtSides ? playerA.getId() : playerB.getId());

        gameResults.push_back({
            playerA.getId(),
            game->getRealScore(sideA),
            playerB.getId(),
            game->getRealScore(sideB),
            winnerPlayerId,
        });

        game.reset();
    }

    const GameResult *getLastGameResult() const {
        return gameResults.empty() ? nullptr : &gameResults.back();
    }

    MatchResult getMatchResult() const {
        uint8_t aWins = 0;
        uint8_t bWins = 0;

        for (const auto &result: gameResults) {
            if (result.winnerPlayerId == playerA.getId()) aWins++;
            else if (result.winnerPlayerId == playerB.getId()) bWins++;
        }

        return {
            playerA.getId(),
            aWins,
            playerB.getId(),
            bWins,
            aWins > bWins ? playerA.getId() : playerB.getId(),
        };
    }

    UserProfile &getPlayerA() const {
        return playerA;
    }

    UserProfile &getLeftCourtSidePlayer() const {
        return !playersSwappedCourtSides ? playerA : playerB;
    }

    UserProfile &getPlayerB() const {
        return playerB;
    }

    UserProfile &getRightCourtSidePlayer() const {
        return !playersSwappedCourtSides ? playerB : playerA;
    }

    void setPlayersSwappedCourtSides(const bool _playersSwappedCourtSides = false) {
        this->playersSwappedCourtSides = _playersSwappedCourtSides;
    }

    bool getPlayersSwappedCourtSides() const {
        return playersSwappedCourtSides;
    }
};

#endif //MATCH_H
