#ifndef TOURNAMENT_H
#define TOURNAMENT_H
#include <deque>
#include <vector>
#include <esp_random.h>

#include "Garmin/GarminProtocol.h"
#include "Match/Match.h"
#include "Match/MatchOrderKeeper.h"
#include "Utils.h"

/**
 * Multiple players play against each other in many rounds.
 * Lives as long as the selected device mode.
 */
class Tournament {
    std::deque<Match> matches;
    std::vector<UserProfile *> players;
    std::unique_ptr<Rules> rules;
    // Every match's finished games in finish order, removed ones included so indices stay valid.
    TournamentLog log;
    // Lets a watch tell a new tournament from games appended to the one it cached.
    uint32_t id;

    Match *activeMatch = nullptr;

    UserProfile *selectedByUid(const uint32_t uid) const {
        for (UserProfile *player: players) {
            if (player->getUid() == uid) return player;
        }
        return nullptr;
    }

    static uint32_t newId() {
        uint32_t v = 0;
        while (v == 0) v = esp_random();
        return v;
    }

public:
    // Every listed game must fit a RESULTS turn (bits 0-6) and stay visible.
    static constexpr size_t MAX_LISTED = 128;

    Tournament(const Tournament &) = delete;
    Tournament &operator=(const Tournament &) = delete;

    std::unique_ptr<MatchOrderKeeper> matchOrderKeeper;

    explicit Tournament(std::unique_ptr<Rules> r) : rules(std::move(r)), id(newId()),
                                                    matchOrderKeeper(std::make_unique<MatchOrderKeeper>()) {
    }

    Match &createMatch(UserProfile &userProfileA, UserProfile &userProfileB) {
        printLn("Creating new match between %s and %s", userProfileA.getName(), userProfileB.getName());
        const auto newMatchId = matches.size();
        matches.emplace_back(newMatchId, userProfileA, userProfileB, rules.get(), &log);
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

    Match *getActiveMatch() const {
        return activeMatch;
    }

    // The pair's match in either order; `swapped` when it has them as B/A.
    Match *findMatch(const UserProfile &userProfileA, const UserProfile &userProfileB, bool &swapped) {
        for (auto &match: matches) {
            const auto &matchPlayerA = match.getPlayerA();
            const auto &matchPlayerB = match.getPlayerB();

            if (matchPlayerA.getId() == userProfileA.getId() && matchPlayerB.getId() == userProfileB.getId()) {
                swapped = false;
                return &match;
            }

            if (matchPlayerA.getId() == userProfileB.getId() && matchPlayerB.getId() == userProfileA.getId()) {
                swapped = true;
                return &match;
            }
        }
        return nullptr;
    }

    Match &chooseMatchBetween(UserProfile &userProfileA, UserProfile &userProfileB) {
        bool swapped = false;
        Match *found = findMatch(userProfileA, userProfileB, swapped);
        if (found != nullptr) {
            found->setPlayersSwappedCourtSides(swapped);
            activeMatch = found;
            return *found;
        }

        Match &match = createMatch(userProfileA, userProfileB);
        activeMatch = &match;
        return match;
    }

    /**
     * ADD_OFFLINE_GAME: a finished game between two selected players, played off the board.
     * Stored in the pair's match (created if needed, activeMatch untouched) as its next turn.
     */
    Garmin::AckStatus addOfflineGame(const uint32_t uidA, const uint32_t uidB, const uint8_t scoreA,
                                     const uint8_t scoreB) {
        if (uidA == uidB) return Garmin::AckStatus::Invalid;

        UserProfile *profileA = selectedByUid(uidA);
        UserProfile *profileB = selectedByUid(uidB);
        if (profileA == nullptr || profileB == nullptr) return Garmin::AckStatus::Invalid;
        if (!rules->isFinalScore(scoreA, scoreB)) return Garmin::AckStatus::Invalid;
        if (listedCount() >= MAX_LISTED) return Garmin::AckStatus::Invalid;

        bool swapped = false;
        Match *match = findMatch(*profileA, *profileB, swapped);
        if (match == nullptr) {
            match = &createMatch(*profileA, *profileB);
        }
        match->recordGame(swapped ? scoreB : scoreA, swapped ? scoreA : scoreB);
        return Garmin::AckStatus::Applied;
    }

    // REMOVE_OFFLINE_GAME: `index` is a listed index under (tournamentId, rev); tracked games stay.
    Garmin::AckStatus removeOfflineGame(const uint32_t tournamentId, const uint16_t rev, const uint16_t index) {
        if (tournamentId != id || rev != log.rev || index >= listedCount()) return Garmin::AckStatus::Invalid;

        const FinishedGame &entry = listedGame(index);
        for (auto &match: matches) {
            if (&match != entry.match) continue;
            if (!match.markRemoved(entry.game)) return Garmin::AckStatus::Invalid;
            log.rev++;
            return Garmin::AckStatus::Applied;
        }
        return Garmin::AckStatus::Invalid;
    }

    size_t getMatchCount() const {
        return matches.size();
    }

    uint32_t getId() const {
        return id;
    }

    // The raw log, removed games included.
    size_t getFinishedGameCount() const {
        return log.finished.size();
    }

    const FinishedGame &getFinishedGame(const size_t index) const {
        return log.finished[index];
    }

    // The RESULTS view: the log without removed games.
    size_t listedCount() const {
        size_t count = 0;
        for (const auto &entry: log.finished) {
            if (!entry.match->getGameResult(entry.game).removed) count++;
        }
        return count;
    }

    // `index` < listedCount().
    const FinishedGame &listedGame(const size_t index) const {
        size_t listed = 0;
        for (const auto &entry: log.finished) {
            if (entry.match->getGameResult(entry.game).removed) continue;
            if (listed++ == index) return entry;
        }
        return log.finished.back();
    }

    // The pair's turn of a listed game, 1-based, removed games not counted.
    uint16_t turnOf(const size_t index) const {
        const FinishedGame &entry = listedGame(index);
        return entry.match->turnOf(entry.game);
    }

    uint16_t getRev() const {
        return log.rev;
    }
};

#endif //TOURNAMENT_H
