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
    // The last applied COMMIT_MIRROR's crc; 0 = none (tablet mode, spec 20).
    uint32_t mirrorCrc = 0;

    Match *activeMatch = nullptr;

    UserProfile *selectedByUid(const uint32_t uid) const {
        for (UserProfile *player: players) {
            if (player->getUid() == uid) return player;
        }
        return nullptr;
    }

    static bool isOutbox(const GameResult &r) {
        return r.local && r.outboxSeq != 0 && !r.acked;
    }

    // An un-ACKed board game's seq order is its log order: seqs are handed out on append, and
    // a mirror re-appends the survivors in seq order after every other game.
    template<class F>
    void forEachOutbox(F f) const {
        for (const auto &entry: log.finished) {
            const GameResult &r = entry.match->getGameResult(entry.game);
            if (isOutbox(r) && !f(entry, r)) return;
        }
    }

    // A game to append in the mirror rebuild.
    struct Placed {
        Match *match;
        GameResult result;
    };

    // What RESULTS shows of a listed game, plus its id.
    struct ListedKey {
        const Match *match;
        uint32_t id;
        uint8_t scoreA;
        uint8_t scoreB;
        bool offline;

        bool operator==(const ListedKey &o) const {
            return match == o.match && id == o.id && scoreA == o.scoreA && scoreB == o.scoreB
                   && offline == o.offline;
        }
    };

    std::vector<ListedKey> listedKeys() const {
        std::vector<ListedKey> keys;
        keys.reserve(log.finished.size());
        for (const auto &entry: log.finished) {
            const GameResult &r = entry.match->getGameResult(entry.game);
            if (Match::isListed(r)) keys.push_back({entry.match, r.id, r.playerAScore, r.playerBScore, r.offline});
        }
        return keys;
    }

    const GameResult *findGame(const uint32_t gameId) const {
        for (const auto &entry: log.finished) {
            const GameResult &r = entry.match->getGameResult(entry.game);
            if (r.id == gameId) return &r;
        }
        return nullptr;
    }

    static const uint8_t *mirrorEntry(const uint8_t *entries, const uint8_t index) {
        return entries + static_cast<size_t>(index) * Garmin::GAME_ENTRY_SIZE;
    }

    static bool mirrorHasId(const uint8_t count, const uint8_t *entries, const uint32_t gameId) {
        for (uint8_t i = 0; i < count; i++) {
            if (Garmin::getU32(mirrorEntry(entries, i)) == gameId) return true;
        }
        return false;
    }

    // count <= 128, crc, non-zero unique ids, two different roster uids, a final score.
    bool validMirror(const uint8_t count, const uint32_t crc, const uint8_t *entries) const {
        if (count > MAX_LISTED || (count > 0 && entries == nullptr)) return false;
        if (Garmin::mirrorCrc(count, entries) != crc) return false;
        for (uint8_t i = 0; i < count; i++) {
            const Garmin::GameEntry g = Garmin::parseGameEntry(mirrorEntry(entries, i));
            if (g.id == 0 || g.uidA == g.uidB || selectedByUid(g.uidA) == nullptr
                || selectedByUid(g.uidB) == nullptr || !rules->isFinalScore(g.scoreA, g.scoreB)) {
                return false;
            }
            for (uint8_t j = 0; j < i; j++) {
                if (Garmin::getU32(mirrorEntry(entries, j)) == g.id) return false;
            }
        }
        return true;
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

    // Tablet mode (spec 20) under the tablet's tournament id; 0 = board mode, as the one-argument form.
    Tournament(std::unique_ptr<Rules> r, const uint32_t tabletTid)
        : rules(std::move(r)), id(tabletTid != 0 ? tabletTid : newId()),
          matchOrderKeeper(std::make_unique<MatchOrderKeeper>()) {
        log.tablet = tabletTid != 0;
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
    // Tablet mode: the tablet is the only editor (spec 20.6).
    Garmin::AckStatus removeOfflineGame(const uint32_t tournamentId, const uint16_t rev, const uint16_t index) {
        if (log.tablet) return Garmin::AckStatus::WrongScreen;
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

    // --- Tablet mode (spec 20); the commands answer WRONG_SCREEN in board mode ---

    bool isTablet() const {
        return log.tablet;
    }

    // Newest seq handed out; 0 = none yet.
    uint16_t outboxTo() const {
        return log.outboxTo;
    }

    // Oldest un-ACKed seq; outboxTo() + 1 when nothing is un-ACKed.
    uint16_t outboxFrom() const {
        uint16_t from = static_cast<uint16_t>(log.outboxTo + 1);
        forEachOutbox([&from](const FinishedGame &, const GameResult &r) -> bool {
            from = r.outboxSeq;
            return false;
        });
        return from;
    }

    uint16_t outboxCount() const {
        uint16_t n = 0;
        forEachOutbox([&n](const FinishedGame &, const GameResult &) -> bool {
            n++;
            return true;
        });
        return n;
    }

    // The `index`-th un-ACKed board game in seq order (< outboxCount()) as an OUTBOX entry:
    // the match's own A/B, OFFLINE for a watch's game, never PENDING.
    bool outboxEntry(const uint16_t index, uint16_t &seq, uint8_t out[Garmin::GAME_ENTRY_SIZE]) const {
        uint16_t i = 0;
        bool found = false;
        forEachOutbox([&](const FinishedGame &entry, const GameResult &r) -> bool {
            if (i++ != index) return true;
            seq = r.outboxSeq;
            const Garmin::GameEntry g = {r.id, entry.match->getPlayerA().getUid(), entry.match->getPlayerB().getUid(),
                                         r.playerAScore, r.playerBScore,
                                         r.offline ? static_cast<uint8_t>(Garmin::GameFlags::Offline) : uint8_t{0}};
            Garmin::buildGameEntry(g, out);
            found = true;
            return false;
        });
        return found;
    }

    uint32_t getMirrorCrc() const {
        return mirrorCrc;
    }

    // STATUS with the session's sport and roster; all zero in board mode.
    Garmin::TabletStatus tabletStatus(const uint8_t sport, const uint8_t rosterCount, const uint32_t rosterCrc) const {
        if (!log.tablet) return Garmin::TabletStatus{};
        return {id, sport, rosterCount, rosterCrc, outboxFrom(), outboxTo(), mirrorCrc};
    }

    // ACK_OUTBOX: every seq <= upTo is stored on the tablet. Games, tally and RESULTS stay.
    Garmin::AckStatus ackOutbox(const uint32_t tournamentId, const uint16_t upTo) {
        if (!log.tablet) return Garmin::AckStatus::WrongScreen;
        if (tournamentId != id || upTo > log.outboxTo) return Garmin::AckStatus::Invalid;
        for (auto &match: matches) match.ackUpTo(upTo);
        return Garmin::AckStatus::Applied;
    }

    /**
     * COMMIT_MIRROR: `entries` = count * 15 B staged games. INVALID changes nothing. Else the
     * games become the mirror's in index order, then the un-ACKed board games it lacks in seq
     * order; Match objects, activeMatch, the running Game and the side latches stay. rev
     * bumps only when the listed sequence changed.
     */
    Garmin::AckStatus applyMirror(const uint32_t tournamentId, const uint8_t count, const uint32_t crc,
                                  const uint8_t *entries) {
        if (!log.tablet) return Garmin::AckStatus::WrongScreen;
        if (tournamentId != id || !validMirror(count, crc, entries)) return Garmin::AckStatus::Invalid;

        const std::vector<ListedKey> before = listedKeys();
        std::vector<Placed> placed;
        placed.reserve(static_cast<size_t>(count) + outboxCount());
        for (uint8_t i = 0; i < count; i++) {
            const Garmin::GameEntry g = Garmin::parseGameEntry(mirrorEntry(entries, i));
            UserProfile &a = *selectedByUid(g.uidA);
            UserProfile &b = *selectedByUid(g.uidB);
            bool swapped = false;
            Match *match = findMatch(a, b, swapped);
            if (match == nullptr) match = &createMatch(a, b);
            const uint8_t scoreA = swapped ? g.scoreB : g.scoreA;
            const uint8_t scoreB = swapped ? g.scoreA : g.scoreB;
            const uint8_t winner = rules->checkWinner(scoreA, scoreB) == GameSide::a
                                       ? match->getPlayerA().getId()
                                       : match->getPlayerB().getId();
            GameResult r = {match->getPlayerA().getId(), scoreA, match->getPlayerB().getId(), scoreB, winner};
            r.id = g.id;
            r.offline = (g.flags & static_cast<uint8_t>(Garmin::GameFlags::Offline)) != 0;
            r.pending = (g.flags & static_cast<uint8_t>(Garmin::GameFlags::Pending)) != 0;
            const GameResult *own = findGame(g.id);
            if (own != nullptr && own->local) {
                // A board game's id in the mirror acks it.
                r.local = true;
                r.outboxSeq = own->outboxSeq;
                r.acked = true;
            }
            placed.push_back({match, r});
        }
        // Never dropped, even past MAX_LISTED: RESULTS serves the first 128 until the tablet ACKs.
        forEachOutbox([&](const FinishedGame &entry, const GameResult &r) -> bool {
            if (!mirrorHasId(count, entries, r.id)) placed.push_back({const_cast<Match *>(entry.match), r});
            return true;
        });

        // Hard-rule exception: in tablet mode the mirror commit is a third writer of the game
        // set; ACKed board games the tablet dropped are gone.
        log.finished.clear();
        for (auto &match: matches) match.clearForMirror();
        for (const Placed &p: placed) p.match->appendMirrored(p.result);
        mirrorCrc = crc;
        if (!(listedKeys() == before)) log.rev++;
        return Garmin::AckStatus::Applied;
    }

    /**
     * SET_SIDE: court side 0 left / 1 right becomes `uid`, the other side's player stays on
     * their side. activeMatch moves to that pair (the next game's pair on CELEBRATION /
     * GAME_OVER, where no Game runs) and a running Game moves with it, score kept per court side.
     */
    Garmin::AckStatus reassignSide(const uint8_t side, const uint32_t uid) {
        if (!log.tablet) return Garmin::AckStatus::WrongScreen;
        UserProfile *profile = selectedByUid(uid);
        if (side > 1 || activeMatch == nullptr || profile == nullptr) return Garmin::AckStatus::Invalid;
        UserProfile &kept = side == 0 ? activeMatch->getRightCourtSidePlayer() : activeMatch->getLeftCourtSidePlayer();
        UserProfile &current = side == 0 ? activeMatch->getLeftCourtSidePlayer() : activeMatch->getRightCourtSidePlayer();
        if (profile == &kept) return Garmin::AckStatus::Invalid;
        if (profile == &current) return Garmin::AckStatus::Applied;

        std::unique_ptr<Game> running = activeMatch->releaseGame();
        Match &to = side == 0 ? chooseMatchBetween(*profile, kept) : chooseMatchBetween(kept, *profile);
        to.adoptGame(std::move(running));
        return Garmin::AckStatus::Applied;
    }

    // The raw log, removed games included.
    size_t getFinishedGameCount() const {
        return log.finished.size();
    }

    const FinishedGame &getFinishedGame(const size_t index) const {
        return log.finished[index];
    }

    // The RESULTS view: the log without removed and pending games.
    size_t listedCount() const {
        size_t count = 0;
        for (const auto &entry: log.finished) {
            if (Match::isListed(entry.match->getGameResult(entry.game))) count++;
        }
        return count;
    }

    // `index` < listedCount().
    const FinishedGame &listedGame(const size_t index) const {
        size_t listed = 0;
        for (const auto &entry: log.finished) {
            if (!Match::isListed(entry.match->getGameResult(entry.game))) continue;
            if (listed++ == index) return entry;
        }
        return log.finished.back();
    }

    // The pair's turn of a listed game, 1-based, counting listed games only.
    uint16_t turnOf(const size_t index) const {
        const FinishedGame &entry = listedGame(index);
        return entry.match->turnOf(entry.game);
    }

    uint16_t getRev() const {
        return log.rev;
    }
};

#endif //TOURNAMENT_H
