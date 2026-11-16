#ifndef MATCH_H
#define MATCH_H

#include "MatchResult.h"
#include "UserProfile.h"
#include "Tournament/Game/Game.h"
#include "Tournament/Rules/Rules.h"
#include "Tournament/Game/GameResult.h"
#include "Tournament/TournamentLog.h"
#include "Utils.h"

/**
 * Two players and their running game. The pair's finished games are the tournament's records
 * (TournamentLog), matched by uid in either A/B order. Lives as long as the tournament does.
 */
class Match {
    uint8_t id;

    UserProfile &playerA;
    UserProfile &playerB;

    std::unique_ptr<Game> game = nullptr;

    Rules *rules;

    // The tournament's records; null outside a Tournament (nothing is recorded).
    TournamentLog *log;

    bool playersSwappedCourtSides = false;

    // The orientation of the first tracked game, for the watch's SIDES_SWAPPED (spec 11.3). RAM only.
    bool startSwapped = false;
    // Offline games have no court sides, so only a tracked one fixes startSwapped.
    bool hasTrackedGame = false;

    // The last game finished on this board, as played: a peer's later edit of its record does
    // not change what GAME_OVER shows.
    GameResult lastTracked = {};

    // +1 when the record has this pair as A/B, -1 as B/A, 0 for another pair.
    int orientation(const GameRecord &r) const {
        if (r.uidA == playerA.getUid() && r.uidB == playerB.getUid()) return 1;
        if (r.uidA == playerB.getUid() && r.uidB == playerA.getUid()) return -1;
        return 0;
    }

public:
    Match(const uint8_t id, UserProfile &userAProfile, UserProfile &userBProfile, Rules *rules,
          TournamentLog *log = nullptr)
        : id(id), playerA(userAProfile), playerB(userBProfile), rules(rules), log(log) {
    }

    Match(const Match &) = delete;
    Match &operator=(const Match &) = delete;

    uint8_t getId() const {
        return id;
    }

    Game *createGame() {
        game = std::make_unique<Game>(rules);
        return game.get();
    }

    // The winning point is committed: the game becomes a record (and frees the match slot).
    void finishGame() {
        const GameSide sideA = playersSwappedCourtSides ? GameSide::b : GameSide::a;
        const GameSide sideB = playersSwappedCourtSides ? GameSide::a : GameSide::b;

        const uint8_t winnerPlayerId = game->getWinner() == GameSide::a
            ? (playersSwappedCourtSides ? playerB.getId() : playerA.getId())
            : (playersSwappedCourtSides ? playerA.getId() : playerB.getId());

        // At finish, not createGame: a game left by UNDO at 0:0 must not fix the orientation.
        if (!hasTrackedGame) {
            startSwapped = playersSwappedCourtSides;
            hasTrackedGame = true;
        }

        const uint8_t scoreA = game->getRealScore(sideA);
        const uint8_t scoreB = game->getRealScore(sideB);
        lastTracked = {playerA.getId(), scoreA, playerB.getId(), scoreB, winnerPlayerId};
        if (log != nullptr) {
            // A full log, or no tournament running, drops it (TournamentLog's gate); START_MATCH
            // is refused before that, so only a match begun without a reservation gets here.
            log->create(playerA.getUid(), playerB.getUid(), scoreA, scoreB, 0, TournamentLog::BOARD);
            log->setReserved(false);
        }

        game.reset();
    }

    // SET_SIDE: the running game (score per court side) moves to another pair's match.
    std::unique_ptr<Game> releaseGame() {
        return std::move(game);
    }

    void adoptGame(std::unique_ptr<Game> running) {
        game = std::move(running);
    }

    bool hasGame() const {
        return game != nullptr;
    }

    // A copy, never a pointer. The last game tracked on this board.
    bool lastGameResult(GameResult &out) const {
        if (!hasTrackedGame) return false;
        out = lastTracked;
        return true;
    }

    // Live records of this pair, either A/B order; the higher score wins.
    MatchResult getMatchResult() const {
        uint8_t aWins = 0;
        uint8_t bWins = 0;

        for (size_t i = 0; log != nullptr && i < log->storedCount(); i++) {
            const GameRecord &r = log->stored(i).record;
            const int o = r.deleted() ? 0 : orientation(r);
            if (o == 0 || r.scoreA == r.scoreB) continue;
            const bool firstWon = r.scoreA > r.scoreB;
            if (firstWon == (o > 0)) aWins++;
            else bWins++;
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

    // Reversed against the first tracked game; false until that game is finished.
    bool sidesSwapped() const {
        return hasTrackedGame && playersSwappedCourtSides != startSwapped;
    }
};

#endif //MATCH_H
