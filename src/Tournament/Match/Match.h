#ifndef MATCH_H
#define MATCH_H

#include "MatchResult.h"
#include "UserProfile.h"
#include "Tournament/Game/Game.h"
#include "Tournament/Rules/Rules.h"
#include "Tournament/Game/GameResult.h"
#include "Utils.h"
#include <esp_random.h>

class Match;

// One finished game in tournament order: the match's `game`-th result (0-based).
struct FinishedGame {
    const Match *match;
    uint16_t game;
};

// A tournament's chronological game log (the watch's RESULTS, spec 19). RAM only.
struct TournamentLog {
    std::vector<FinishedGame> finished;
    // Changes on every change to the listed games other than an append.
    uint16_t rev = 0;
    // Tablet mode (spec 20): board games get outbox seqs and a tracked game starts pending.
    bool tablet = false;
    // The newest outbox seq handed out; 0 = none yet.
    uint16_t outboxTo = 0;

    inline bool hasGameId(uint32_t id) const;

    uint32_t newGameId() const {
        uint32_t v = 0;
        while (v == 0 || hasGameId(v)) v = esp_random();
        return v;
    }
};

/**
 * Two preselected players play against each other in many rounds.
 * Lives as long as the tournament does.
 */
class Match {
    uint8_t id;

    UserProfile &playerA;
    UserProfile &playerB;

    std::unique_ptr<Game> game = nullptr;
    // Append-only: finishGame() and recordGame() are the only writers, so a FinishedGame
    // index stays valid; markRemoved() only sets an offline game's `removed`. Tablet mode's
    // mirror commit (clearForMirror/appendMirrored) is the one exception.
    std::vector<GameResult> gameResults;

    Rules *rules;

    // The tournament's chronological log; null outside a Tournament.
    TournamentLog *log;

    bool playersSwappedCourtSides = false;

    // The orientation of the first tracked game, for the watch's SIDES_SWAPPED (spec 11.3). RAM only.
    bool startSwapped = false;
    // Offline games have no court sides, so only a tracked one fixes startSwapped.
    bool hasTrackedGame = false;

    uint32_t newGameId() const {
        if (log != nullptr) return log->newGameId();
        uint32_t v = 0;
        while (v == 0 || hasGameId(v)) v = esp_random();
        return v;
    }

    void append(GameResult result) {
        result.local = true;
        if (log != nullptr && log->tablet) result.outboxSeq = ++log->outboxTo;
        gameResults.push_back(result);
        logLast();
    }

    void logLast() {
        if (log != nullptr) {
            log->finished.push_back({this, static_cast<uint16_t>(gameResults.size() - 1)});
        }
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

        // At finish, not createGame: a game left by UNDO at 0:0 must not fix the orientation.
        if (!hasTrackedGame) {
            startSwapped = playersSwappedCourtSides;
            hasTrackedGame = true;
        }

        append({
            playerA.getId(),
            game->getRealScore(sideA),
            playerB.getId(),
            game->getRealScore(sideB),
            winnerPlayerId,
            newGameId(),
            false,
            false,
            log != nullptr && log->tablet,
        });

        game.reset();
    }

    // An offline game in this match's own A/B order; the caller checked Rules::isFinalScore().
    void recordGame(const uint8_t scoreA, const uint8_t scoreB) {
        const uint8_t winnerPlayerId = rules->checkWinner(scoreA, scoreB) == GameSide::a
                                           ? playerA.getId()
                                           : playerB.getId();
        append({playerA.getId(), scoreA, playerB.getId(), scoreB, winnerPlayerId, newGameId(), true, false});
    }

    // Tablet mode's mirror commit (Tournament::applyMirror) only, never in board mode: the
    // tablet's list replaces this match's games; the caller rebuilds the log.
    void clearForMirror() {
        gameResults.clear();
    }

    void appendMirrored(const GameResult &result) {
        gameResults.push_back(result);
        logLast();
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

    // ACK_OUTBOX: board games with seq <= upTo are stored on the tablet.
    void ackUpTo(const uint16_t upTo) {
        for (auto &result: gameResults) {
            if (result.outboxSeq != 0 && result.outboxSeq <= upTo) result.acked = true;
        }
    }

    // Only an offline game can be removed; false otherwise or when already removed.
    bool markRemoved(const size_t index) {
        if (index >= gameResults.size()) return false;
        GameResult &result = gameResults[index];
        if (!result.offline || result.removed) return false;
        result.removed = true;
        return true;
    }

    bool hasGameId(const uint32_t gameId) const {
        for (const auto &result: gameResults) {
            if (result.id == gameId) return true;
        }
        return false;
    }

    size_t getGameCount() const {
        return gameResults.size();
    }

    const GameResult &getGameResult(const size_t index) const {
        return gameResults[index];
    }

    // 1-based, counting this match's listed games up to `index`.
    uint16_t turnOf(const size_t index) const {
        uint16_t turn = 0;
        for (size_t i = 0; i <= index && i < gameResults.size(); i++) {
            if (isListed(gameResults[i])) turn++;
        }
        return turn;
    }

    // RESULTS lists it; a pending game counts only in the court's tally.
    static bool isListed(const GameResult &result) {
        return !result.removed && !result.pending;
    }

    // A copy, never a pointer: an append can reallocate gameResults under a holder.
    // The last game tracked on this board, so a GAME_OVER after an offline add or a
    // mirror still shows its own game.
    bool lastGameResult(GameResult &out) const {
        for (size_t i = gameResults.size(); i > 0; i--) {
            if (!gameResults[i - 1].offline && gameResults[i - 1].local) {
                out = gameResults[i - 1];
                return true;
            }
        }
        return false;
    }

    MatchResult getMatchResult() const {
        uint8_t aWins = 0;
        uint8_t bWins = 0;

        for (const auto &result: gameResults) {
            if (result.removed) continue;
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

    // Reversed against the first tracked game; false until that game is finished.
    bool sidesSwapped() const {
        return hasTrackedGame && playersSwappedCourtSides != startSwapped;
    }
};

bool TournamentLog::hasGameId(const uint32_t id) const {
    for (const auto &entry: finished) {
        if (entry.match->getGameResult(entry.game).id == id) return true;
    }
    return false;
}

#endif //MATCH_H
