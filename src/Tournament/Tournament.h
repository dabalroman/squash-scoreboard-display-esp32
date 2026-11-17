#ifndef TOURNAMENT_H
#define TOURNAMENT_H
#include <deque>
#include <vector>
#include <esp_random.h>

#include "Garmin/GarminProtocol.h"
#include "Match/Match.h"
#include "Match/MatchOrderKeeper.h"
#include "TournamentLog.h"
#include "TournamentRoster.h"
#include "Utils.h"

using TournamentStatus = Garmin::TournamentStatus;

/**
 * A sport session's tournament (docs/garmin-protocol.md 20): id, sport, roster, game records and
 * change log. Lives as long as the selected device mode; runs from start() or join() to leave().
 */
class Tournament {
    std::deque<Match> matches;
    TournamentRoster roster;
    std::unique_ptr<Rules> rules;
    TournamentLog log;
    // 0 = no tournament running.
    uint32_t id = 0;
    uint8_t sport = 0;
    bool joined = false;
    // A connection still bound to the previous log must not match the next one (spec 20 Binding).
    uint8_t lastEpoch = 0;

    Match *activeMatch = nullptr;

    static uint32_t newId() {
        uint32_t v = 0;
        while (v == 0) v = esp_random();
        return v;
    }

    uint8_t newEpoch() {
        uint8_t v = 0;
        while (v == 0 || v == lastEpoch) v = static_cast<uint8_t>(esp_random());
        lastEpoch = v;
        return v;
    }

    // A live record's roster and score checks (20.2 dropped writes).
    bool validLive(const uint32_t uidA, const uint32_t uidB, const uint8_t scoreA, const uint8_t scoreB) const {
        return uidA != uidB && roster.committedByUid(uidA) != nullptr && roster.committedByUid(uidB) != nullptr
               && rules->isFinalScore(scoreA, scoreB);
    }

    void playersChanged() {
        matchOrderKeeper->setPlayers(roster.profiles());
    }

public:
    Tournament(const Tournament &) = delete;
    Tournament &operator=(const Tournament &) = delete;

    std::unique_ptr<MatchOrderKeeper> matchOrderKeeper;

    explicit Tournament(std::unique_ptr<Rules> r)
        : rules(std::move(r)), matchOrderKeeper(std::make_unique<MatchOrderKeeper>()) {
    }

    // --- Lifecycle (20.3, 20.6) ---

    bool running() const {
        return id != 0;
    }

    bool isJoined() const {
        return joined;
    }

    uint32_t getId() const {
        return id;
    }

    uint8_t getSport() const {
        return sport;
    }

    // START on CHOOSE_PLAYERS: the first one draws the id and log_epoch; every START makes the
    // selection the roster.
    void start(const uint8_t sportId) {
        if (!running()) {
            id = newId();
            sport = sportId;
            log.start(newEpoch());
        }
        roster.commit();
        playersChanged();
    }

    // BEGIN_TOURNAMENT on MENU: the peer's tournament with no records, every entry selected.
    bool join(const uint32_t tournamentId, const uint8_t sportId, const uint8_t count, const uint32_t rosterCrc,
              const uint8_t *entries) {
        if (running() || tournamentId == 0 || sportId == 0 || roster.size() != 0) return false;
        if (!roster.adopt(count, rosterCrc, entries)) return false;
        id = tournamentId;
        sport = sportId;
        joined = true;
        log.start(newEpoch());
        playersChanged();
        return true;
    }

    // BEGIN_TOURNAMENT on a sport screen: roster growth under the same id and sport.
    Garmin::AckStatus growRoster(const uint32_t tournamentId, const uint8_t sportId, const uint8_t count,
                                 const uint32_t rosterCrc, const uint8_t *entries) {
        if (!running() || tournamentId != id || sportId != sport) return Garmin::AckStatus::Invalid;
        if (!roster.adopt(count, rosterCrc, entries)) return Garmin::AckStatus::Invalid;
        playersChanged();
        return Garmin::AckStatus::Applied;
    }

    // Records, roster, change log and guard go; Match references die with the matches.
    void leave() {
        activeMatch = nullptr;
        matches.clear();
        roster.clear();
        log.clear();
        id = 0;
        sport = 0;
        joined = false;
        playersChanged();
    }

    TournamentStatus status() const {
        if (!running()) return TournamentStatus{};
        return {id,
                sport,
                roster.committedCount(),
                roster.crc(),
                log.lastSeq(),
                static_cast<uint8_t>(log.liveCount()),
                log.gamesCrc(),
                log.epoch()};
    }

    // --- Roster and picker ---

    const TournamentRoster &getRoster() const {
        return roster;
    }

    // The picker: false at 16 selected.
    bool addPlayer(const UserProfile &userProfile) {
        const bool added = roster.select(userProfile);
        playersChanged();
        return added;
    }

    // False for a roster entry: the roster only grows (TOGGLE_PLAYER INVALID).
    bool removePlayer(const UserProfile &userProfile) {
        const bool removed = roster.unselect(userProfile.getUid());
        playersChanged();
        return removed;
    }

    bool isPlayerIn(const UserProfile &userProfile) const {
        return roster.byUid(userProfile.getUid()) != nullptr;
    }

    bool isRosterEntry(const UserProfile &userProfile) const {
        return roster.committedByUid(userProfile.getUid()) != nullptr;
    }

    // The tournament's own copies: roster order, then the selection not started yet.
    std::vector<UserProfile *> &getPlayers() {
        return roster.profiles();
    }

    // --- Matches ---

    Match &createMatch(UserProfile &userProfileA, UserProfile &userProfileB) {
        printLn("Creating new match between %s and %s", userProfileA.getName(), userProfileB.getName());
        const auto newMatchId = matches.size();
        matches.emplace_back(newMatchId, userProfileA, userProfileB, rules.get(), &log);
        return matches.back();
    }

    Match *getActiveMatch() const {
        return activeMatch;
    }

    // The pair's match in either order; `swapped` when it has them as B/A.
    Match *findMatch(const UserProfile &userProfileA, const UserProfile &userProfileB, bool &swapped) {
        for (auto &match: matches) {
            const uint32_t a = match.getPlayerA().getUid();
            const uint32_t b = match.getPlayerB().getUid();
            if (a == userProfileA.getUid() && b == userProfileB.getUid()) {
                swapped = false;
                return &match;
            }
            if (a == userProfileB.getUid() && b == userProfileA.getUid()) {
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

    size_t getMatchCount() const {
        return matches.size();
    }

    /**
     * START_MATCH: keeps a live slot and a change seq for the game until its record exists
     * (Match::finishGame) or releaseMatchSlot(). INVALID at the live cap or with the log full.
     */
    Garmin::AckStatus reserveMatchSlot() {
        if (log.isReserved()) return Garmin::AckStatus::Applied;
        if (!running() || log.full() || log.liveCount() >= TournamentLog::LIVE_MAX) {
            return Garmin::AckStatus::Invalid;
        }
        log.setReserved(true);
        return Garmin::AckStatus::Applied;
    }

    // The game left without a record (back from INTRO, a leave).
    void releaseMatchSlot() {
        log.setReserved(false);
    }

    bool matchSlotReserved() const {
        return log.isReserved();
    }

    /**
     * SET_SIDE (20.7): court side 0 left / 1 right becomes `uid`, the other side's player stays
     * on their side. activeMatch moves to that pair (the next game's pair on CELEBRATION /
     * GAME_OVER, where no Game runs) and a running Game moves with it, score kept per court side.
     */
    Garmin::AckStatus reassignSide(const uint8_t side, const uint32_t uid) {
        UserProfile *profile = roster.committedByUid(uid);
        if (side > 1 || activeMatch == nullptr || profile == nullptr) return Garmin::AckStatus::Invalid;
        UserProfile &kept = side == 0 ? activeMatch->getRightCourtSidePlayer() : activeMatch->getLeftCourtSidePlayer();
        UserProfile &current = side == 0 ? activeMatch->getLeftCourtSidePlayer() : activeMatch->getRightCourtSidePlayer();
        if (uid == kept.getUid()) return Garmin::AckStatus::Invalid;
        if (uid == current.getUid()) return Garmin::AckStatus::Applied;

        std::unique_ptr<Game> running = activeMatch->releaseGame();
        Match &to = side == 0 ? chooseMatchBetween(*profile, kept) : chooseMatchBetween(kept, *profile);
        if (running) to.adoptGame(std::move(running));
        return Garmin::AckStatus::Applied;
    }

    // --- Records (20.2) ---

    /**
     * ADD_OFFLINE_GAME: an OFFLINE record between two roster entries, in the pair's match's own
     * A/B order when the board has one. `writer` = the command's pairing slot.
     */
    Garmin::AckStatus addOfflineGame(const uint32_t uidA, const uint32_t uidB, const uint8_t scoreA,
                                     const uint8_t scoreB, const uint8_t writer = TournamentLog::BOARD) {
        if (!running() || !validLive(uidA, uidB, scoreA, scoreB)) return Garmin::AckStatus::Invalid;
        if (log.full() || log.liveCount() >= log.liveCap()) return Garmin::AckStatus::Invalid;

        bool swapped = false;
        const Match *match = findMatch(*roster.committedByUid(uidA), *roster.committedByUid(uidB), swapped);
        if (match != nullptr && swapped) {
            log.create(uidB, uidA, scoreB, scoreA, GameRecord::OFFLINE, writer);
        } else {
            log.create(uidA, uidB, scoreA, scoreB, GameRecord::OFFLINE, writer);
        }
        return Garmin::AckStatus::Applied;
    }

    // REMOVE_OFFLINE_GAME: tombstones the OFFLINE record at RESULTS `index` under (tid, rev).
    Garmin::AckStatus removeOfflineGame(const uint32_t tournamentId, const uint16_t rev, const uint16_t index,
                                        const uint8_t writer = TournamentLog::BOARD) {
        if (!running() || tournamentId != id || rev != log.resultsRev() || index >= listedCount()) {
            return Garmin::AckStatus::Invalid;
        }
        const size_t at = log.liveAt(index);
        GameRecord r = log.stored(at).record;
        if (!r.offline() || log.full()) return Garmin::AckStatus::Invalid;
        r.flags |= GameRecord::DELETED;
        log.rewrite(at, r, writer);
        return Garmin::AckStatus::Applied;
    }

    // A RECORD write: `writer` = the connection's pairing slot. Writes on a connection not bound
    // to this tournament are the caller's to drop.
    TournamentLog::Merge mergeRecord(const uint8_t image[GameRecord::SIZE], const uint8_t writer) {
        if (!running()) return TournamentLog::Merge::Dropped;
        const GameRecord r = GameRecord::fromBytes(image);
        if (!r.deleted() && !validLive(r.uidA, r.uidB, r.scoreA, r.scoreB)) return TournamentLog::Merge::Dropped;
        return log.merge(r, writer);
    }

    // ACK_CHANGES' model part; the screen check is the caller's.
    // `epoch`: the log_epoch the sender's STATUS read reported (spec 20 Binding).
    Garmin::AckStatus ackChanges(const uint32_t tournamentId, const uint8_t epoch, const uint16_t upTo) {
        if (!running() || tournamentId != id || epoch != log.epoch() || !log.ack(upTo))
            return Garmin::AckStatus::Invalid;
        return Garmin::AckStatus::Applied;
    }

    // The unsynced guard (20.6): armed by the first ACK, holds while changes_to > acked_to.
    bool unsynced() const {
        return log.unsynced();
    }

    uint16_t unsyncedCount() const {
        return log.unsyncedCount();
    }

    // Pairing a slot again.
    void resetWriter(const uint8_t slot) {
        log.resetWriter(slot);
    }

    // CHANGES / RECORDS walks and END's fields.
    const TournamentLog &getLog() const {
        return log;
    }

    // --- The RESULTS view (section 19): live records by (order, id) ---

    size_t listedCount() const {
        return log.liveCount();
    }

    // `index` < listedCount().
    const GameRecord &listedRecord(const size_t index) const {
        return log.stored(log.liveAt(index)).record;
    }

    // The pair's k-th listed game, either A/B order, 1-based, capped at 127.
    uint8_t turnOf(const size_t index) const {
        const size_t at = log.liveAt(index);
        const GameRecord &game = log.stored(at).record;
        uint8_t turn = 0;
        for (size_t i = 0; i <= at; i++) {
            const GameRecord &r = log.stored(i).record;
            if (r.deleted()) continue;
            const bool samePair = (r.uidA == game.uidA && r.uidB == game.uidB)
                                  || (r.uidA == game.uidB && r.uidB == game.uidA);
            if (samePair && turn < Garmin::TURN_MASK) turn++;
        }
        return turn;
    }

    uint16_t getRev() const {
        return log.resultsRev();
    }
};

#endif //TOURNAMENT_H
