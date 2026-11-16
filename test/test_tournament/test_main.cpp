// Tournament (docs/garmin-protocol.md 20): game records, merge, change log, STATUS, roster
// lifecycle and the RESULTS view (19) with WatchSupport::resultEntry on top. The 16.15 sync_*
// vectors' model semantics. Board-agnostic.
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

// The Tournament headers log through printLn and leave its include to the caller.
#define LOGGER_HELPER
inline void printLn(const char *, ...) {}

#include "DeviceMode/WatchSupport.h"
#include "Tournament/Rules/PadelRules.h"
#include "Tournament/Rules/ShortVolleyballRules.h"
#include "Tournament/Rules/SquashRules.h"
#include "Tournament/Rules/VolleyballRules.h"
#include "Tournament/Tournament.h"
#include "../common/check.h"
#include "../common/host_globals.h"

void setUp() {}
void tearDown() {}

namespace {
    using Garmin::AckStatus;
    using Merge = TournamentLog::Merge;

    const uint8_t SQUASH = 1;
    const uint32_t ANNA = 0x0A0B0C0D;
    const uint32_t KRYS = 0x11223344;
    const uint32_t OLA = 0x55667788;
    const uint32_t TID = 0x7AB1E001;
    const uint8_t T1 = 0;
    const uint8_t T2 = 1;
    const uint8_t BOARD = TournamentLog::BOARD;

    // One commit: `winner` ends on winnerPoints, the other side on loserPoints.
    void play(Match &match, const GameSide winner, const int winnerPoints, const int loserPoints) {
        Game *game = match.createGame();
        const GameSide loser = winner == GameSide::a ? GameSide::b : GameSide::a;
        for (int i = 0; i < loserPoints; i++) game->scorePoint(loser);
        for (int i = 0; i < winnerPoints; i++) game->scorePoint(winner);
        TEST_ASSERT_TRUE(game->commit() == winner);
        match.finishGame();
    }

    void expectAck(const AckStatus want, const AckStatus got, const char *what) {
        CHECK(want == got, "%s: ack %u, expected %u", what, static_cast<unsigned>(got), static_cast<unsigned>(want));
    }

    void expectMerge(const Merge want, const Merge got, const char *what) {
        CHECK(want == got, "%s: merge %u, expected %u", what, static_cast<unsigned>(got),
              static_cast<unsigned>(want));
    }

    // A RESULTS entry as the wire has it: turn byte with the OFFLINE bit.
    struct Entry {
        uint32_t uidA, uidB;
        uint8_t scoreA, scoreB, turn;
    };

    const uint8_t OFF = Garmin::TURN_OFFLINE;

    Entry entry(const Tournament &tournament, const size_t index) {
        uint8_t out[Garmin::RESULT_ENTRY_SIZE];
        WatchSupport::resultEntry(tournament, index, out);
        return {Garmin::getU32(out), Garmin::getU32(out + 4), out[8], out[9], out[10]};
    }

    void expectEntry(const Entry want, const Entry got, const char *what) {
        CHECK(want.uidA == got.uidA && want.uidB == got.uidB && want.scoreA == got.scoreA
              && want.scoreB == got.scoreB && want.turn == got.turn,
              "%s: got %08x-%08x %u:%u turn %02x, expected %08x-%08x %u:%u turn %02x", what,
              static_cast<unsigned>(got.uidA), static_cast<unsigned>(got.uidB), got.scoreA, got.scoreB, got.turn,
              static_cast<unsigned>(want.uidA), static_cast<unsigned>(want.uidB), want.scoreA, want.scoreB, want.turn);
    }

    struct Image {
        uint8_t b[GameRecord::SIZE];
    };

    Image image(const uint32_t id, const uint8_t rev, const uint16_t order, const uint32_t uidA, const uint32_t uidB,
                const uint8_t scoreA, const uint8_t scoreB, const uint8_t flags = 0) {
        GameRecord r;
        r.id = id;
        r.rev = rev;
        r.order = order;
        r.uidA = uidA;
        r.uidB = uidB;
        r.scoreA = scoreA;
        r.scoreB = scoreB;
        r.flags = flags;
        Image out = {};
        r.toBytes(out.b);
        return out;
    }

    // Spec 16.15 record bytes (after the RECORD type byte).
    Image hex(const char *s) {
        Image out = {};
        TEST_ASSERT_EQUAL(GameRecord::SIZE * 3 - 1, strlen(s));
        for (size_t i = 0; i < GameRecord::SIZE; i++) {
            unsigned v = 0;
            TEST_ASSERT_EQUAL(1, sscanf(s + i * 3, "%2x", &v));
            out.b[i] = static_cast<uint8_t>(v);
        }
        return out;
    }

    const char *const READ_1 = "01 00 ed 5e 00 01 00 0d 0c 0b 0a 44 33 22 11 0b 07 00";
    const char *const READ_2 = "02 00 ed 5e 00 02 00 44 33 22 11 88 77 66 55 0b 05 00";
    const char *const READ_3 = "03 00 ed 5e 00 03 00 0d 0c 0b 0a 88 77 66 55 0b 06 02";
    const char *const EDIT = "01 00 ed 5e 01 01 00 0d 0c 0b 0a 44 33 22 11 0b 08 00";
    const char *const TIE = "01 00 ed 5e 01 01 00 0d 0c 0b 0a 44 33 22 11 0b 09 00";
    const char *const READ_4 = "03 00 ed 5e 01 03 00 0d 0c 0b 0a 88 77 66 55 0b 06 03";

    std::vector<uint8_t> rosterWire(const std::vector<UserProfile> &players) {
        std::vector<uint8_t> out(players.size() * Garmin::ROSTER_ENTRY_SIZE);
        for (size_t i = 0; i < players.size(); i++) {
            const UserProfile &p = players[i];
            const Color c = p.getColor();
            Garmin::buildRosterEntry(p.getUid(), c.r, c.g, c.b, p.getName(), &out[i * Garmin::ROSTER_ENTRY_SIZE]);
        }
        return out;
    }

    // Spec 16.4's stored players.
    const UserProfile S_ANNA(0, ANNA, "ANNA", Color(0xff, 0x00, 0x00));
    const UserProfile S_KRYS(1, KRYS, "KRYSTIAN", Color(0x00, 0x40, 0xff));
    const UserProfile S_OLA(2, OLA, "OLA", Color(0x00, 0xc0, 0x30));

    bool join(Tournament &t, const std::vector<UserProfile> &players, const uint32_t tid = TID) {
        const std::vector<uint8_t> w = rosterWire(players);
        const uint8_t n = static_cast<uint8_t>(players.size());
        return t.join(tid, SQUASH, n, Garmin::rosterVersion(n, w.data()), w.data());
    }

    AckStatus grow(Tournament &t, const std::vector<UserProfile> &players, const uint32_t tid = TID,
                   const uint8_t sport = SQUASH) {
        const std::vector<uint8_t> w = rosterWire(players);
        const uint8_t n = static_cast<uint8_t>(players.size());
        return t.growRoster(tid, sport, n, Garmin::rosterVersion(n, w.data()), w.data());
    }

    // A board tournament started from the picker: ANNA and KRYSTIAN (+ OLA).
    struct Fixture {
        Tournament tournament;

        explicit Fixture(Rules *rules, const bool withOla = false) : tournament(std::unique_ptr<Rules>(rules)) {
            tournament.addPlayer(S_ANNA);
            tournament.addPlayer(S_KRYS);
            if (withOla) tournament.addPlayer(S_OLA);
            tournament.start(SQUASH);
        }

        UserProfile &p(const uint32_t uid) {
            return *tournament.getRoster().committedByUid(uid);
        }

        uint16_t base() const {
            return static_cast<uint16_t>(tournament.status().logEpoch << 8);
        }
    };

    // Board 1 of 16.15 after sync_join: the tablet's join target, record 0x5EED0001 at seq 1.
    struct Board {
        Tournament tournament{std::unique_ptr<Rules>(new SquashRules())};

        Board() {
            TEST_ASSERT_TRUE(join(tournament, {S_ANNA, S_KRYS, S_OLA}));
        }

        Merge put(const char *bytes, const uint8_t writer) {
            return tournament.mergeRecord(hex(bytes).b, writer);
        }

        Merge put(const Image &i, const uint8_t writer) {
            return tournament.mergeRecord(i.b, writer);
        }
    };

    bool sameImage(const GameRecord &r, const char *bytes) {
        uint8_t got[GameRecord::SIZE];
        r.toBytes(got);
        return memcmp(got, hex(bytes).b, GameRecord::SIZE) == 0;
    }
}

// --- RESULTS view, board games, offline games ---

// Chronological across matches, turn per pair, A/B fixed by the pair's first set-up.
void test_log_order_and_turns() {
    Fixture f(new SquashRules(), true);
    TEST_ASSERT_EQUAL(0, f.tournament.listedCount());

    play(f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS)), GameSide::a, 11, 7);
    play(f.tournament.chooseMatchBetween(f.p(KRYS), f.p(OLA)), GameSide::a, 11, 4);
    // The pair again with sides swapped: KRYSTIAN left wins 11:9, still A = ANNA.
    play(f.tournament.chooseMatchBetween(f.p(KRYS), f.p(ANNA)), GameSide::a, 11, 9);

    TEST_ASSERT_EQUAL(3, f.tournament.listedCount());
    expectEntry({ANNA, KRYS, 11, 7, 1}, entry(f.tournament, 0), "game 0");
    expectEntry({KRYS, OLA, 11, 4, 1}, entry(f.tournament, 1), "game 1");
    expectEntry({ANNA, KRYS, 9, 11, 2}, entry(f.tournament, 2), "game 2 (16.13)");
    // Board games: rev 0, orders 1..3, writer BOARD, appends only.
    for (uint16_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(i + 1, f.tournament.listedRecord(i).order);
        TEST_ASSERT_EQUAL(0, f.tournament.listedRecord(i).rev);
        TEST_ASSERT_EQUAL(BOARD, f.tournament.getLog().stored(i).writer);
    }
    TEST_ASSERT_EQUAL_HEX16(f.base(), f.tournament.getRev());
    TEST_ASSERT_EQUAL(3, f.tournament.status().changesTo);
}

// A game left by UNDO at 0:0 is never finished, so it is not listed.
void test_abandoned_game_not_listed() {
    Fixture f(new SquashRules());
    Match &match = f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS));

    Game *game = match.createGame();
    game->scorePoint(GameSide::a);
    game->losePoint(GameSide::a);
    TEST_ASSERT_EQUAL(0, f.tournament.listedCount());

    play(match, GameSide::b, 11, 3);
    TEST_ASSERT_EQUAL(1, f.tournament.listedCount());
    expectEntry({ANNA, KRYS, 3, 11, 1}, entry(f.tournament, 0), "the finished game is turn 1");
}

// Padel: a set's gems, 7-6 after a tiebreak.
void test_padel_gems() {
    Fixture f(new PadelRules());
    play(f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS)), GameSide::b, 7, 6);
    expectEntry({ANNA, KRYS, 6, 7, 1}, entry(f.tournament, 0), "tiebreak set");
}

// Drawn at the first START, never 0, kept by a later START; leave() ends it.
void test_id_and_epoch() {
    Tournament t(std::unique_ptr<Rules>(new SquashRules()));
    TEST_ASSERT_FALSE(t.running());
    TEST_ASSERT_EQUAL(0, t.getId());
    t.addPlayer(S_ANNA);
    t.addPlayer(S_KRYS);
    // The shim's next draw from this state is 0: both the id and the epoch skip it.
    hostRandomState() = 634785765u;
    TEST_ASSERT_EQUAL_HEX32(0, esp_random());
    hostRandomState() = 634785765u;
    t.start(SQUASH);
    TEST_ASSERT_TRUE(t.running());
    const uint32_t id = t.getId();
    const uint8_t epoch = t.status().logEpoch;
    TEST_ASSERT_NOT_EQUAL(0, id);
    TEST_ASSERT_NOT_EQUAL(0, epoch);
    TEST_ASSERT_EQUAL_HEX16(epoch << 8, t.getRev());

    // A later START (more players picked) keeps the tournament.
    t.addPlayer(S_OLA);
    TEST_ASSERT_EQUAL(2, t.status().rosterCount);
    t.start(SQUASH);
    TEST_ASSERT_EQUAL_HEX32(id, t.getId());
    TEST_ASSERT_EQUAL(epoch, t.status().logEpoch);
    TEST_ASSERT_EQUAL(3, t.status().rosterCount);
    TEST_ASSERT_EQUAL_HEX32(0x150fe75b, t.status().rosterCrc);

    t.leave();
    TEST_ASSERT_FALSE(t.running());
    TEST_ASSERT_EQUAL(0, t.getPlayers().size());
    t.addPlayer(S_ANNA);
    t.addPlayer(S_KRYS);
    t.start(SQUASH);
    TEST_ASSERT_NOT_EQUAL(id, t.getId());
}

namespace {
    struct ScoreCase {
        uint8_t a, b;
        bool final;
    };

    template <size_t N>
    void expectFinalScores(const char *sport, const Rules &rules, const ScoreCase (&cases)[N]) {
        for (const ScoreCase &c : cases) {
            CHECK(rules.isFinalScore(c.a, c.b) == c.final, "%s %u:%u final %d", sport, c.a, c.b, !c.final);
            CHECK(rules.isFinalScore(c.b, c.a) == c.final, "%s %u:%u final %d (mirrored)", sport, c.b, c.a,
                  !c.final);
        }
    }
}

// A won score the winner had not already reached one point earlier.
void test_final_scores() {
    const ScoreCase squash[] = {{11, 0, true}, {11, 9, true}, {12, 10, true}, {11, 10, false}, {15, 3, false},
                                {12, 9, false}, {0, 0, false}, {10, 8, false}, {128, 126, false}};
    expectFinalScores("squash", SquashRules(), squash);
    const ScoreCase volleyball[] = {{25, 23, true}, {27, 25, true}, {25, 0, true}, {26, 23, false}, {24, 22, false}};
    expectFinalScores("volleyball", VolleyballRules(), volleyball);
    const ScoreCase shortVolleyball[] = {{15, 13, true}, {16, 14, true}, {16, 13, false}, {14, 12, false}};
    expectFinalScores("short volleyball", ShortVolleyballRules(), shortVolleyball);
    const ScoreCase padel[] = {{6, 0, true}, {6, 4, true}, {7, 5, true}, {7, 6, true}, {8, 6, false},
                               {7, 0, false}, {6, 5, false}, {6, 6, false}};
    expectFinalScores("padel", PadelRules(), padel);
}

// Validation: two distinct roster entries and a legal final score; no tournament, no add.
void test_add_invalid() {
    Fixture f(new SquashRules());
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, ANNA, 11, 3), "uidA == uidB");
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, OLA, 11, 3), "OLA not in the roster");
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(0x99999999, KRYS, 11, 3), "unknown uid");
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, KRYS, 15, 3), "15:3 squash");
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, KRYS, 10, 8), "unfinished");
    // Selected but not started: not a roster entry yet.
    f.tournament.addPlayer(S_OLA);
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, OLA, 11, 3), "OLA only selected");
    TEST_ASSERT_EQUAL(0, f.tournament.listedCount());
    TEST_ASSERT_EQUAL(0, f.tournament.status().changesTo);

    Fixture padel(new PadelRules());
    expectAck(AckStatus::Invalid, padel.tournament.addOfflineGame(ANNA, KRYS, 8, 6), "padel 8:6");
    expectAck(AckStatus::Invalid, padel.tournament.addOfflineGame(ANNA, KRYS, 7, 0), "padel 7:0");
    expectAck(AckStatus::Applied, padel.tournament.addOfflineGame(ANNA, KRYS, 7, 6), "padel 7:6");
    expectAck(AckStatus::Applied, padel.tournament.addOfflineGame(KRYS, ANNA, 7, 5), "padel 7:5");
    TEST_ASSERT_EQUAL(2, padel.tournament.listedCount());

    Tournament idle(std::unique_ptr<Rules>(new SquashRules()));
    idle.addPlayer(S_ANNA);
    idle.addPlayer(S_KRYS);
    expectAck(AckStatus::Invalid, idle.addOfflineGame(ANNA, KRYS, 11, 3), "no tournament");
}

// A record only: no match, the board's active match unchanged; A/B as given.
void test_add_without_match() {
    Fixture f(new SquashRules());
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(KRYS, ANNA, 12, 10), "add");
    TEST_ASSERT_NULL(f.tournament.getActiveMatch());
    TEST_ASSERT_EQUAL(0, f.tournament.getMatchCount());
    TEST_ASSERT_EQUAL_HEX16(f.base(), f.tournament.getRev());
    expectEntry({KRYS, ANNA, 12, 10, 1 | OFF}, entry(f.tournament, 0), "the add sets A/B");
    TEST_ASSERT_EQUAL(GameRecord::OFFLINE, f.tournament.listedRecord(0).flags);

    // The pair's match counts it whichever way round.
    Match &active = f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS));
    TEST_ASSERT_EQUAL(1, active.getMatchResult().scoreOf(f.p(KRYS).getId()));
}

// Either order: the scores land in the pair's match's own A/B order.
void test_add_swapped_pair() {
    Fixture f(new SquashRules());
    play(f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS)), GameSide::a, 11, 7);
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(KRYS, ANNA, 11, 5), "swapped add");
    TEST_ASSERT_EQUAL(1, f.tournament.getMatchCount());
    expectEntry({ANNA, KRYS, 11, 7, 1}, entry(f.tournament, 0), "tracked");
    expectEntry({ANNA, KRYS, 5, 11, 2 | OFF}, entry(f.tournament, 1), "offline, swapped into A/B");

    const MatchResult standing = f.tournament.getActiveMatch()->getMatchResult();
    TEST_ASSERT_EQUAL(1, standing.scoreOf(f.p(ANNA).getId()));
    TEST_ASSERT_EQUAL(1, standing.scoreOf(f.p(KRYS).getId()));
}

// A game on court while an ADD lands finishes after it; GAME_OVER still shows the played game.
void test_live_game_after_add() {
    Fixture f(new SquashRules());
    Match &match = f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS));
    Game *game = match.createGame();
    for (int i = 0; i < 11; i++) game->scorePoint(GameSide::b);
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 3), "add mid-game");
    TEST_ASSERT_TRUE(game->commit() == GameSide::b);
    match.finishGame();

    expectEntry({ANNA, KRYS, 11, 3, 1 | OFF}, entry(f.tournament, 0), "offline");
    expectEntry({ANNA, KRYS, 0, 11, 2}, entry(f.tournament, 1), "tracked, next turn");

    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(KRYS, ANNA, 11, 4), "add on GAME_OVER");
    GameResult last = {};
    TEST_ASSERT_TRUE(match.lastGameResult(last));
    TEST_ASSERT_EQUAL(0, last.playerAScore);
    TEST_ASSERT_EQUAL(11, last.playerBScore);
}

// Offline games alone give a GAME_OVER view nothing to show.
void test_last_game_result_none() {
    Fixture f(new SquashRules());
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 3), "add");
    GameResult last = {};
    TEST_ASSERT_FALSE(f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS)).lastGameResult(last));
}

// Remove: a tombstone, the pair's later turns renumbered, rev bumped, other pairs untouched.
void test_remove_renumbers_and_bumps_rev() {
    Fixture f(new SquashRules(), true);
    play(f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS)), GameSide::a, 11, 7);
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 2), "add");
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(KRYS, OLA, 11, 6), "other pair");
    play(f.tournament.chooseMatchBetween(f.p(KRYS), f.p(ANNA)), GameSide::a, 11, 9);
    TEST_ASSERT_EQUAL(4, f.tournament.listedCount());
    expectEntry({ANNA, KRYS, 9, 11, 3}, entry(f.tournament, 3), "before remove");

    const uint32_t tid = f.tournament.getId();
    const uint16_t rev = f.base();
    expectAck(AckStatus::Applied, f.tournament.removeOfflineGame(tid, rev, 1), "remove");
    TEST_ASSERT_EQUAL_HEX16(rev + 1, f.tournament.getRev());
    TEST_ASSERT_EQUAL(3, f.tournament.listedCount());
    TEST_ASSERT_EQUAL(4, f.tournament.getLog().storedCount());
    expectEntry({ANNA, KRYS, 11, 7, 1}, entry(f.tournament, 0), "tracked 1");
    expectEntry({KRYS, OLA, 11, 6, 1 | OFF}, entry(f.tournament, 1), "other pair");
    expectEntry({ANNA, KRYS, 9, 11, 2}, entry(f.tournament, 2), "renumbered");

    const MatchResult standing = f.tournament.getActiveMatch()->getMatchResult();
    TEST_ASSERT_EQUAL(1, standing.scoreOf(f.p(ANNA).getId()));
    TEST_ASSERT_EQUAL(1, standing.scoreOf(f.p(KRYS).getId()));

    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid, rev, 1), "stale rev");
    expectAck(AckStatus::Applied, f.tournament.removeOfflineGame(tid, rev + 1, 1), "remove other pair");
    TEST_ASSERT_EQUAL_HEX16(rev + 2, f.tournament.getRev());
    TEST_ASSERT_EQUAL(2, f.tournament.listedCount());
}

void test_remove_invalid() {
    Fixture f(new SquashRules());
    play(f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS)), GameSide::a, 11, 7);
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 2), "add");
    const uint32_t tid = f.tournament.getId();
    const uint16_t rev = f.base();

    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid, rev, 0), "tracked");
    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid + 1, rev, 1), "stale tournament");
    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid, rev + 1, 1), "rev ahead");
    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid, rev, 2), "index == count");
    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid, rev, 0xFFFF), "index far out");
    TEST_ASSERT_EQUAL_HEX16(rev, f.tournament.getRev());
    TEST_ASSERT_EQUAL(2, f.tournament.listedCount());

    expectAck(AckStatus::Applied, f.tournament.removeOfflineGame(tid, rev, 1), "offline");
    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid, rev + 1, 1), "now past the end");
}

// SIDES_SWAPPED keys on the first tracked game; offline games have no court sides.
void test_offline_only_sides_not_swapped() {
    Fixture f(new SquashRules());
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 3), "add");
    Match &match = f.tournament.chooseMatchBetween(f.p(KRYS), f.p(ANNA));
    TEST_ASSERT_FALSE(match.getPlayersSwappedCourtSides());
    TEST_ASSERT_FALSE(match.sidesSwapped());

    play(match, GameSide::a, 11, 5);
    TEST_ASSERT_FALSE(match.sidesSwapped());
    f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS));
    TEST_ASSERT_TRUE(match.sidesSwapped());
}

// RESULTS_MAX live records; the turn byte caps at 127; a removal frees a slot.
void test_listed_cap() {
    Fixture f(new SquashRules());
    for (size_t i = 0; i < Garmin::RESULTS_MAX; i++) {
        expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 0), "fill");
    }
    TEST_ASSERT_EQUAL(128, f.tournament.listedCount());
    TEST_ASSERT_EQUAL(127, f.tournament.turnOf(126));
    TEST_ASSERT_EQUAL(127, f.tournament.turnOf(127));
    TEST_ASSERT_EQUAL(127 | OFF, entry(f.tournament, 127).turn);
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, KRYS, 11, 0), "129th");

    expectAck(AckStatus::Applied, f.tournament.removeOfflineGame(f.tournament.getId(), f.base(), 0), "remove");
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 0), "slot freed");
    TEST_ASSERT_EQUAL(128, f.tournament.listedCount());
}

// Every created record: non-zero and unique among the stored ones, redrawn on a clash.
void test_game_ids() {
    Fixture f(new SquashRules());
    const uint32_t before = hostRandomState();
    play(f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS)), GameSide::a, 11, 7);
    // The next draw repeats the tracked game's id: the add must skip it.
    hostRandomState() = before;
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 2), "add after a clash");
    // Draws 0 next (test_id_and_epoch): skipped too.
    hostRandomState() = 634785765u;
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 4), "add after a 0");

    TEST_ASSERT_EQUAL(3, f.tournament.listedCount());
    for (size_t i = 0; i < 3; i++) {
        TEST_ASSERT_NOT_EQUAL(0, f.tournament.listedRecord(i).id);
        for (size_t j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(f.tournament.listedRecord(j).id, f.tournament.listedRecord(i).id);
        }
    }
}

// --- 16.15 sync vectors ---

// sync_join / sync_begin: STATUS, the empty games_crc, a forwarded record, no echo.
void test_sync_join_and_begin() {
    Tournament idle(std::unique_ptr<Rules>(new SquashRules()));
    const TournamentStatus none = idle.status();
    TEST_ASSERT_EQUAL_HEX32(0, none.tid);
    TEST_ASSERT_EQUAL_HEX32(0, none.gamesCrc);
    TEST_ASSERT_EQUAL(0, none.logEpoch);

    Board b;
    TournamentStatus s = b.tournament.status();
    TEST_ASSERT_EQUAL_HEX32(TID, s.tid);
    TEST_ASSERT_EQUAL(SQUASH, s.sport);
    TEST_ASSERT_EQUAL(3, s.rosterCount);
    TEST_ASSERT_EQUAL_HEX32(0x150fe75b, s.rosterCrc);
    TEST_ASSERT_EQUAL(0, s.changesTo);
    TEST_ASSERT_EQUAL(0, s.gamesCount);
    TEST_ASSERT_EQUAL_HEX32(0xd202ef8d, s.gamesCrc);
    TEST_ASSERT_NOT_EQUAL(0, s.logEpoch);

    expectMerge(Merge::Stored, b.put(READ_1, T1), "push_1");
    s = b.tournament.status();
    TEST_ASSERT_EQUAL(1, s.changesTo);
    TEST_ASSERT_EQUAL(1, s.gamesCount);
    TEST_ASSERT_EQUAL_HEX32(0xd015c673, s.gamesCrc);

    // The pusher's own write is skipped; another reader gets it, unchanged.
    GameRecord r;
    uint16_t seq = 0;
    TEST_ASSERT_FALSE(b.tournament.getLog().changeAt(1, T1, r, seq));
    TEST_ASSERT_TRUE(b.tournament.getLog().changeAt(1, T2, r, seq));
    TEST_ASSERT_EQUAL(1, seq);
    TEST_ASSERT_TRUE(sameImage(r, READ_1));
    TEST_ASSERT_FALSE(b.tournament.getLog().changeAt(2, T2, r, seq));
    TEST_ASSERT_EQUAL(0, b.tournament.getLog().lostSeq());

    // Join refused while one runs, and on MENU with a bad roster.
    TEST_ASSERT_FALSE(join(b.tournament, {S_ANNA, S_KRYS}, TID + 1));
    Tournament other(std::unique_ptr<Rules>(new SquashRules()));
    const std::vector<uint8_t> w = rosterWire({S_ANNA, S_KRYS});
    TEST_ASSERT_FALSE(other.join(TID, SQUASH, 2, Garmin::rosterVersion(2, w.data()) ^ 1, w.data()));
    TEST_ASSERT_FALSE(other.join(0, SQUASH, 2, Garmin::rosterVersion(2, w.data()), w.data()));
    TEST_ASSERT_FALSE(other.running());
}

// sync_pull: resuming re-reads at most what the reader had; a re-merge is a no-op.
void test_sync_pull() {
    Board b;
    expectMerge(Merge::Stored, b.put(READ_1, BOARD), "seq 1");
    expectMerge(Merge::Stored, b.put(READ_2, BOARD), "seq 2");
    expectMerge(Merge::Stored, b.put(READ_3, BOARD), "seq 3");
    const TournamentStatus s = b.tournament.status();
    TEST_ASSERT_EQUAL(3, s.changesTo);
    TEST_ASSERT_EQUAL(3, s.gamesCount);
    TEST_ASSERT_EQUAL_HEX32(0xef752d79, s.gamesCrc);

    GameRecord r;
    uint16_t seq = 0;
    TEST_ASSERT_TRUE(b.tournament.getLog().changeAt(2, T1, r, seq));
    TEST_ASSERT_EQUAL(2, seq);
    TEST_ASSERT_TRUE(sameImage(r, READ_2));
    TEST_ASSERT_TRUE(b.tournament.getLog().changeAt(seq + 1, T1, r, seq));
    TEST_ASSERT_EQUAL(3, seq);
    TEST_ASSERT_TRUE(sameImage(r, READ_3));
    TEST_ASSERT_FALSE(b.tournament.getLog().changeAt(seq + 1, T1, r, seq));

    expectMerge(Merge::NoOp, b.put(READ_2, T1), "seq 2 again");
    TEST_ASSERT_EQUAL(3, b.tournament.status().changesTo);
    // Turns over the pulled records: the offline one is ANNA - OLA's first.
    expectEntry({ANNA, OLA, 11, 6, 1 | OFF}, entry(b.tournament, 2), "pulled offline game");
}

// sync_merge: newer rev replaces, older is a no-op, a same-rev tie goes to the larger image.
void test_sync_merge() {
    Board b;
    expectMerge(Merge::Stored, b.put(READ_1, BOARD), "seq 1");
    expectMerge(Merge::Replaced, b.put(EDIT, T1), "edit");
    TEST_ASSERT_EQUAL(2, b.tournament.status().changesTo);
    expectMerge(Merge::NoOp, b.put(READ_1, T2), "stale");
    TEST_ASSERT_EQUAL(2, b.tournament.status().changesTo);
    expectMerge(Merge::Replaced, b.put(TIE, T2), "tie");
    // The loser of the tie, again: no-op.
    expectMerge(Merge::NoOp, b.put(EDIT, T1), "tie loser");
    expectMerge(Merge::NoOp, b.put(TIE, T1), "identical");

    const TournamentStatus s = b.tournament.status();
    TEST_ASSERT_EQUAL(3, s.changesTo);
    TEST_ASSERT_EQUAL(1, s.gamesCount);
    TEST_ASSERT_EQUAL_HEX32(0xd3990a8b, s.gamesCrc);
    TEST_ASSERT_EQUAL(1, b.tournament.getLog().storedCount());
    TEST_ASSERT_TRUE(sameImage(b.tournament.getLog().stored(0).record, TIE));
    // An edit renumbers nothing but changes the view: rev bumped twice.
    TEST_ASSERT_EQUAL_HEX16((s.logEpoch << 8) + 2, b.tournament.getRev());

    // sync_leave/read: seqs 2 and 3 both changed it; one record, latest form, T2's write.
    GameRecord r;
    uint16_t seq = 0;
    TEST_ASSERT_TRUE(b.tournament.getLog().changeAt(2, T1, r, seq));
    TEST_ASSERT_EQUAL(3, seq);
    TEST_ASSERT_TRUE(sameImage(r, TIE));
    TEST_ASSERT_FALSE(b.tournament.getLog().changeAt(seq + 1, T1, r, seq));
    TEST_ASSERT_FALSE(b.tournament.getLog().changeAt(2, T2, r, seq));
}

// sync_tombstone: REMOVE_OFFLINE_GAME on board 1; the tombstone forwarded to another board.
void test_sync_tombstone() {
    Board b;
    b.put(READ_1, BOARD);
    b.put(READ_2, BOARD);
    b.put(READ_3, T1);
    const uint8_t epoch = b.tournament.status().logEpoch;
    TEST_ASSERT_EQUAL_HEX16(epoch << 8, b.tournament.getRev());

    expectAck(AckStatus::Applied, b.tournament.removeOfflineGame(TID, static_cast<uint16_t>(epoch << 8), 2, 3),
              "remove");
    // RESULTS: count 2, rev log_epoch:01.
    TEST_ASSERT_EQUAL(2, b.tournament.listedCount());
    TEST_ASSERT_EQUAL_HEX16((epoch << 8) + 1, b.tournament.getRev());
    const TournamentStatus s = b.tournament.status();
    TEST_ASSERT_EQUAL(4, s.changesTo);
    TEST_ASSERT_EQUAL(2, s.gamesCount);
    TEST_ASSERT_EQUAL_HEX32(0xa7806044, s.gamesCrc);

    GameRecord r;
    uint16_t seq = 0;
    TEST_ASSERT_TRUE(b.tournament.getLog().changeAt(4, T1, r, seq));
    TEST_ASSERT_EQUAL(4, seq);
    TEST_ASSERT_TRUE(sameImage(r, READ_4));
    TEST_ASSERT_EQUAL(0, b.tournament.getLog().lostSeq());

    // Sticky delete: a live write never revives it, whatever the rev.
    expectMerge(Merge::NoOp, b.put(image(0x5EED0003, 9, 3, ANNA, OLA, 11, 6, GameRecord::OFFLINE), T1), "revive");
    expectMerge(Merge::NoOp, b.put(READ_4, T1), "tombstone again");
    TEST_ASSERT_EQUAL(4, b.tournament.status().changesTo);

    // Board 2 (sync_pull's records): the forwarded tombstone replaces the live record.
    Board two;
    two.put(READ_1, T1);
    two.put(READ_2, T1);
    two.put(READ_3, T1);
    const uint16_t rev2 = two.tournament.getRev();
    expectMerge(Merge::Replaced, two.put(READ_4, T1), "forwarded tombstone");
    TEST_ASSERT_EQUAL_HEX32(0xa7806044, two.tournament.status().gamesCrc);
    TEST_ASSERT_EQUAL_HEX16(rev2 + 1, two.tournament.getRev());

    // An unknown id's tombstone is stored too (roster not checked), RESULTS untouched.
    expectMerge(Merge::Stored, two.put(image(0x77, 1, 9, 0x1, 0x2, 0, 0, GameRecord::DELETED), T1), "new tombstone");
    TEST_ASSERT_EQUAL_HEX16(rev2 + 1, two.tournament.getRev());
    TEST_ASSERT_EQUAL(2, two.tournament.listedCount());
}

// Dropped writes take no seq.
void test_merge_drops() {
    Board b;
    b.put(READ_1, BOARD);
    expectMerge(Merge::Dropped, b.put(image(0, 0, 5, ANNA, KRYS, 11, 3), T1), "id 0");
    expectMerge(Merge::Dropped, b.put(image(9, 0, 0, ANNA, KRYS, 11, 3), T1), "order 0");
    expectMerge(Merge::Dropped, b.put(image(9, 0, 5, ANNA, KRYS, 11, 3, 0x04), T1), "reserved flag");
    expectMerge(Merge::Dropped, b.put(image(0x5EED0001, 1, 2, ANNA, KRYS, 11, 8), T1), "order change");
    expectMerge(Merge::Dropped, b.put(image(9, 0, 5, ANNA, ANNA, 11, 3), T1), "equal uids");
    expectMerge(Merge::Dropped, b.put(image(9, 0, 5, ANNA, 0x99, 11, 3), T1), "uid not in the roster");
    expectMerge(Merge::Dropped, b.put(image(9, 0, 5, ANNA, KRYS, 11, 10), T1), "non-final score");
    expectMerge(Merge::Dropped, b.put(image(0x5EED0001, 1, 1, ANNA, KRYS, 11, 10), T1), "non-final edit");
    TEST_ASSERT_EQUAL(1, b.tournament.status().changesTo);

    Tournament idle(std::unique_ptr<Rules>(new SquashRules()));
    expectMerge(Merge::Dropped, idle.mergeRecord(hex(READ_1).b, T1), "no tournament");
}

// rev is serial: 0 follows 255; a distance of 128 is a tie.
void test_merge_serial_rev() {
    Board b;
    b.put(image(0x10, 255, 1, ANNA, KRYS, 11, 3), T1);
    expectMerge(Merge::Replaced, b.put(image(0x10, 0, 1, ANNA, KRYS, 11, 4), T1), "255 -> 0");
    expectMerge(Merge::NoOp, b.put(image(0x10, 255, 1, ANNA, KRYS, 11, 5), T1), "255 after 0");
    // 128 apart: the byte images decide, and the rev byte precedes the scores.
    expectMerge(Merge::Replaced, b.put(image(0x10, 128, 1, ANNA, KRYS, 11, 2), T1), "128 apart, larger rev byte");
    expectMerge(Merge::NoOp, b.put(image(0x10, 0, 1, ANNA, KRYS, 11, 9), T1), "128 apart, smaller rev byte");
    expectMerge(Merge::NoOp, b.put(image(0x10, 128, 1, ANNA, KRYS, 11, 1), T1), "same rev, smaller image");
    expectMerge(Merge::Replaced, b.put(image(0x10, 128, 1, ANNA, KRYS, 11, 5), T1), "same rev, larger image");
    TEST_ASSERT_EQUAL(5, b.tournament.getLog().stored(0).record.scoreB);
}

// Order then id, unsigned; a record sorting before the last renumbers turns (rev bump).
void test_sort_and_rev() {
    Board b;
    b.put(image(0x80000000, 0, 2, ANNA, KRYS, 11, 1), T1);
    const uint16_t rev = b.tournament.getRev();
    b.put(image(0x00000005, 0, 2, ANNA, KRYS, 11, 2), T1);
    TEST_ASSERT_EQUAL_HEX16(rev + 1, b.tournament.getRev());
    b.put(image(0x00000009, 0, 0xFFFF, ANNA, KRYS, 11, 3), T1);
    TEST_ASSERT_EQUAL_HEX16(rev + 1, b.tournament.getRev());
    b.put(image(0xFFFFFFFF, 0, 1, ANNA, KRYS, 11, 4), T1);
    TEST_ASSERT_EQUAL_HEX16(rev + 2, b.tournament.getRev());

    const uint8_t want[] = {4, 2, 1, 3};
    for (size_t i = 0; i < 4; i++) TEST_ASSERT_EQUAL(want[i], b.tournament.listedRecord(i).scoreB);
    TEST_ASSERT_EQUAL(4, entry(b.tournament, 3).turn);

    // A board-made record goes after the top order, capped at 65535.
    expectAck(AckStatus::Applied, b.tournament.addOfflineGame(KRYS, OLA, 11, 0), "add at the top");
    TEST_ASSERT_EQUAL(0xFFFF, b.tournament.listedRecord(4).order);
}

// 33 tombstones: the one with the oldest seq goes, lost_to = its seq.
void test_tombstone_eviction() {
    Board b;
    b.put(READ_1, BOARD);
    for (uint32_t i = 0; i < TournamentLog::TOMBSTONE_MAX; i++) {
        expectMerge(Merge::Stored, b.put(image(0x100 + i, 1, 5, ANNA, KRYS, 0, 0, GameRecord::DELETED), T1), "fill");
    }
    TEST_ASSERT_EQUAL(33, b.tournament.getLog().storedCount());
    TEST_ASSERT_EQUAL(0, b.tournament.getLog().lostSeq());

    expectMerge(Merge::Stored, b.put(image(0x200, 1, 5, ANNA, KRYS, 0, 0, GameRecord::DELETED), T1), "33rd");
    TEST_ASSERT_EQUAL(33, b.tournament.getLog().storedCount());
    TEST_ASSERT_EQUAL(2, b.tournament.getLog().lostSeq());
    TEST_ASSERT_LESS_THAN(0, b.tournament.getLog().find(0x100));
    TEST_ASSERT_EQUAL(34, b.tournament.status().changesTo);

    // A live record deleted later evicts the next-oldest tombstone.
    expectMerge(Merge::Replaced, b.put(image(0x5EED0001, 1, 1, ANNA, KRYS, 11, 7, GameRecord::DELETED), T1), "del");
    TEST_ASSERT_EQUAL(3, b.tournament.getLog().lostSeq());
    TEST_ASSERT_EQUAL(32, b.tournament.getLog().storedCount());
    TEST_ASSERT_EQUAL(0, b.tournament.listedCount());
    // The evicted id is unknown again: its tombstone stores anew.
    expectMerge(Merge::Stored, b.put(image(0x100, 1, 5, ANNA, KRYS, 0, 0, GameRecord::DELETED), T2), "re-sent");
}

// START_MATCH keeps a slot: 127 live while reserved, the board game's record frees it.
void test_cap_reservation() {
    Fixture f(new SquashRules());
    for (size_t i = 0; i < Garmin::RESULTS_MAX - 1; i++) {
        expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 0), "fill");
    }
    expectAck(AckStatus::Applied, f.tournament.reserveMatchSlot(), "START_MATCH at 127");
    TEST_ASSERT_TRUE(f.tournament.matchSlotReserved());
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, KRYS, 11, 0), "add into the reserved slot");
    const uint16_t seq = f.tournament.status().changesTo;
    expectMerge(Merge::Dropped, f.tournament.mergeRecord(image(0x10, 0, 500, ANNA, KRYS, 11, 0).b, T1),
                "pushed live record into the reserved slot");
    TEST_ASSERT_EQUAL(seq, f.tournament.status().changesTo);
    expectMerge(Merge::Stored, f.tournament.mergeRecord(image(0x11, 1, 500, ANNA, KRYS, 0, 0, 1).b, T1),
                "tombstone takes no slot");

    play(f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS)), GameSide::a, 11, 1);
    TEST_ASSERT_FALSE(f.tournament.matchSlotReserved());
    TEST_ASSERT_EQUAL(128, f.tournament.listedCount());
    expectAck(AckStatus::Invalid, f.tournament.reserveMatchSlot(), "START_MATCH at 128");

    // Back from INTRO: the slot returns.
    expectAck(AckStatus::Applied, f.tournament.removeOfflineGame(f.tournament.getId(), f.tournament.getRev(), 0),
              "remove");
    expectAck(AckStatus::Applied, f.tournament.reserveMatchSlot(), "reserve");
    f.tournament.releaseMatchSlot();
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 0), "released slot");
}

// changes_to 65535 is the end: no START_MATCH, no ADD, RECORD writes dropped.
void test_full_log() {
    Fixture f(new SquashRules());
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 0), "seq 1");
    const GameRecord first = f.tournament.listedRecord(0);
    uint8_t rev = 0;
    while (f.tournament.status().changesTo < TournamentLog::SEQ_MAX - 1) {
        rev++;
        TEST_ASSERT_EQUAL(static_cast<int>(Merge::Replaced),
                          static_cast<int>(f.tournament.mergeRecord(
                              image(first.id, rev, first.order, ANNA, KRYS, 11, rev & 1 ? 1 : 0, 2).b, T1)));
    }
    // The last seq is kept for the board game a reservation holds.
    expectAck(AckStatus::Applied, f.tournament.reserveMatchSlot(), "START_MATCH at 65534");
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, KRYS, 11, 2), "add while the seq is kept");
    expectMerge(Merge::Dropped, f.tournament.mergeRecord(image(first.id, rev + 1, first.order, ANNA, KRYS, 11, 5, 2).b,
                                                         T1), "edit while the seq is kept");
    play(f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS)), GameSide::a, 11, 1);
    TEST_ASSERT_EQUAL(0xFFFF, f.tournament.status().changesTo);
    TEST_ASSERT_EQUAL(2, f.tournament.listedCount());

    expectAck(AckStatus::Invalid, f.tournament.reserveMatchSlot(), "START_MATCH on a full log");
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, KRYS, 11, 2), "ADD on a full log");
    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(f.tournament.getId(), f.tournament.getRev(), 0),
              "REMOVE on a full log");
    expectMerge(Merge::Dropped, f.tournament.mergeRecord(image(0x99, 0, 9, ANNA, KRYS, 11, 2).b, T1), "new id");
    expectMerge(Merge::Dropped, f.tournament.mergeRecord(image(first.id, rev + 1, first.order, ANNA, KRYS, 11, 5,
                                                               2).b, T1), "edit");
    expectMerge(Merge::NoOp, f.tournament.mergeRecord(image(first.id, rev - 1, first.order, ANNA, KRYS, 11, 5,
                                                            2).b, T1), "stale write still a no-op");
    TEST_ASSERT_EQUAL(0xFFFF, f.tournament.status().changesTo);
}

// ACK_CHANGES: board-wide acked_to; the guard arms on the first ACK.
void test_ack_guard() {
    Board b;
    b.put(READ_1, BOARD);
    TEST_ASSERT_FALSE(b.tournament.unsynced());
    TEST_ASSERT_EQUAL(0, b.tournament.unsyncedCount());

    expectAck(AckStatus::Invalid, b.tournament.ackChanges(TID, 2), "past changes_to");
    expectAck(AckStatus::Invalid, b.tournament.ackChanges(TID + 1, 1), "other tournament");
    TEST_ASSERT_FALSE(b.tournament.unsynced());

    expectAck(AckStatus::Applied, b.tournament.ackChanges(TID, 0), "ack 0 arms");
    TEST_ASSERT_TRUE(b.tournament.unsynced());
    TEST_ASSERT_EQUAL(1, b.tournament.unsyncedCount());
    expectAck(AckStatus::Applied, b.tournament.ackChanges(TID, 1), "ack 1");
    TEST_ASSERT_FALSE(b.tournament.unsynced());

    b.put(READ_2, BOARD);
    b.put(READ_3, BOARD);
    TEST_ASSERT_EQUAL(2, b.tournament.unsyncedCount());
    // Board-wide and monotonic: a lower ACK from another tablet changes nothing.
    expectAck(AckStatus::Applied, b.tournament.ackChanges(TID, 0), "lower ack");
    TEST_ASSERT_EQUAL(2, b.tournament.unsyncedCount());
    expectAck(AckStatus::Applied, b.tournament.ackChanges(TID, 3), "ack 3");
    TEST_ASSERT_FALSE(b.tournament.unsynced());

    b.tournament.leave();
    TEST_ASSERT_FALSE(b.tournament.unsynced());
    expectAck(AckStatus::Invalid, b.tournament.ackChanges(TID, 0), "after leave");
}

// Re-pairing a slot: its earlier writes are the board's, so the new holder of the slot reads them.
void test_reset_writer() {
    Board b;
    b.put(READ_1, T1);
    b.put(READ_2, T2);
    GameRecord r;
    uint16_t seq = 0;
    TEST_ASSERT_TRUE(b.tournament.getLog().changeAt(1, T1, r, seq));
    TEST_ASSERT_EQUAL(2, seq);
    b.tournament.resetWriter(T1);
    TEST_ASSERT_TRUE(b.tournament.getLog().changeAt(1, T1, r, seq));
    TEST_ASSERT_EQUAL(1, seq);
    TEST_ASSERT_EQUAL(BOARD, b.tournament.getLog().stored(0).writer);
    TEST_ASSERT_EQUAL(T2, b.tournament.getLog().stored(1).writer);
}

// BEGIN on a sport screen: growth under the same tid and sport only.
void test_roster_growth() {
    Board b;
    b.put(READ_1, BOARD);
    UserProfile *anna = b.tournament.getRoster().committedByUid(ANNA);
    const UserProfile EWA(7, 0x77, "EWA", Color(1, 2, 3));

    expectAck(AckStatus::Invalid, grow(b.tournament, {S_ANNA, S_KRYS, S_OLA, EWA}, TID + 1), "another tid");
    expectAck(AckStatus::Invalid, grow(b.tournament, {S_ANNA, S_KRYS, S_OLA, EWA}, TID, 2), "another sport");
    expectAck(AckStatus::Invalid, grow(b.tournament, {S_ANNA, S_KRYS, EWA}), "OLA dropped");
    TEST_ASSERT_EQUAL(3, b.tournament.status().rosterCount);

    expectAck(AckStatus::Applied, grow(b.tournament, {S_ANNA, S_KRYS, S_OLA, EWA}), "EWA appended");
    const TournamentStatus s = b.tournament.status();
    TEST_ASSERT_EQUAL(4, s.rosterCount);
    const std::vector<uint8_t> w = rosterWire({S_ANNA, S_KRYS, S_OLA, EWA});
    TEST_ASSERT_EQUAL_HEX32(Garmin::rosterVersion(4, w.data()), s.rosterCrc);
    TEST_ASSERT_TRUE(anna == b.tournament.getRoster().committedByUid(ANNA));
    // Records and log untouched.
    TEST_ASSERT_EQUAL(1, s.changesTo);
    TEST_ASSERT_EQUAL_HEX32(0xd015c673, s.gamesCrc);
    expectAck(AckStatus::Applied, b.tournament.addOfflineGame(0x77, OLA, 11, 2), "EWA plays");

    // The roster only grows: an entry cannot be un-picked.
    TEST_ASSERT_FALSE(b.tournament.removePlayer(S_ANNA));
    TEST_ASSERT_TRUE(b.tournament.isRosterEntry(S_ANNA));

    Tournament idle(std::unique_ptr<Rules>(new SquashRules()));
    expectAck(AckStatus::Invalid, grow(idle, {S_ANNA, S_KRYS}), "nothing running");
}

// Leaving drops records, roster, log and guard; a join after it is a fresh log.
void test_leave() {
    Board b;
    b.put(READ_1, BOARD);
    b.tournament.ackChanges(TID, 0);
    b.tournament.chooseMatchBetween(*b.tournament.getRoster().committedByUid(ANNA),
                                    *b.tournament.getRoster().committedByUid(KRYS));
    expectAck(AckStatus::Applied, b.tournament.reserveMatchSlot(), "reserve");
    const uint8_t epoch = b.tournament.getLog().epoch();
    b.tournament.leave();
    const TournamentStatus s = b.tournament.status();
    TEST_ASSERT_EQUAL(0, s.tid);
    TEST_ASSERT_EQUAL(0, s.rosterCount);
    TEST_ASSERT_EQUAL(0, s.changesTo);
    TEST_ASSERT_EQUAL(0, b.tournament.listedCount());
    TEST_ASSERT_EQUAL(0, b.tournament.getMatchCount());
    TEST_ASSERT_NULL(b.tournament.getActiveMatch());
    TEST_ASSERT_FALSE(b.tournament.matchSlotReserved());
    TEST_ASSERT_FALSE(b.tournament.unsynced());

    TEST_ASSERT_TRUE(join(b.tournament, {S_ANNA, S_KRYS}));
    TEST_ASSERT_EQUAL_HEX32(0xd202ef8d, b.tournament.status().gamesCrc);
    TEST_ASSERT_EQUAL(0, b.tournament.status().changesTo);
    TEST_ASSERT_FALSE(b.tournament.unsynced());
    TEST_ASSERT_NOT_EQUAL(0, b.tournament.getLog().epoch());
    TEST_ASSERT_NOT_EQUAL(epoch, b.tournament.getLog().epoch());
}

// SET_SIDE mid-game: the running Game moves to the new pair, score kept per court side.
void test_reassign_running_game() {
    Fixture f(new SquashRules(), true);
    UserProfile &anna = f.p(ANNA);
    UserProfile &krys = f.p(KRYS);
    UserProfile &ola = f.p(OLA);
    Match &first = f.tournament.chooseMatchBetween(krys, anna);  // KRYSTIAN left
    Game *game = first.createGame();
    game->scorePoint(GameSide::a);
    game->scorePoint(GameSide::a);
    game->scorePoint(GameSide::b);
    game->commit();

    expectAck(AckStatus::Invalid, f.tournament.reassignSide(2, OLA), "side 2");
    expectAck(AckStatus::Invalid, f.tournament.reassignSide(1, 0x99999999), "unknown uid");
    expectAck(AckStatus::Invalid, f.tournament.reassignSide(1, KRYS), "the other side's uid");
    expectAck(AckStatus::Applied, f.tournament.reassignSide(1, ANNA), "no-op");
    TEST_ASSERT_EQUAL_PTR(&first, f.tournament.getActiveMatch());
    TEST_ASSERT_TRUE(first.hasGame());

    // OLA replaces ANNA on the right; KRYSTIAN stays left.
    expectAck(AckStatus::Applied, f.tournament.reassignSide(1, OLA), "swap right");
    Match *to = f.tournament.getActiveMatch();
    TEST_ASSERT_NOT_EQUAL(&first, to);
    TEST_ASSERT_FALSE(first.hasGame());
    TEST_ASSERT_TRUE(to->hasGame());
    TEST_ASSERT_EQUAL_PTR(&krys, &to->getLeftCourtSidePlayer());
    TEST_ASSERT_EQUAL_PTR(&ola, &to->getRightCourtSidePlayer());
    TEST_ASSERT_EQUAL(2, game->getRealScore(GameSide::a));
    TEST_ASSERT_EQUAL(1, game->getRealScore(GameSide::b));

    // The left side to ANNA: a new pair ANNA-OLA.
    expectAck(AckStatus::Applied, f.tournament.reassignSide(0, ANNA), "swap left");
    Match *third = f.tournament.getActiveMatch();
    TEST_ASSERT_EQUAL_PTR(&anna, &third->getLeftCourtSidePlayer());
    TEST_ASSERT_EQUAL_PTR(&ola, &third->getRightCourtSidePlayer());

    // KRYSTIAN on the right: back to the first pair's match, now swapped (A = KRYSTIAN).
    expectAck(AckStatus::Applied, f.tournament.reassignSide(1, KRYS), "KRYS right");
    TEST_ASSERT_EQUAL_PTR(&first, f.tournament.getActiveMatch());
    TEST_ASSERT_TRUE(first.getPlayersSwappedCourtSides());
    TEST_ASSERT_EQUAL_PTR(&anna, &first.getLeftCourtSidePlayer());
    TEST_ASSERT_TRUE(first.hasGame());
    TEST_ASSERT_FALSE(to->hasGame());
    TEST_ASSERT_FALSE(third->hasGame());

    // Finishing credits the current pair, sides respected: the left (ANNA) wins 11:1.
    for (int i = 0; i < 9; i++) game->scorePoint(GameSide::a);
    TEST_ASSERT_TRUE(game->commit() == GameSide::a);
    first.finishGame();
    GameResult last = {};
    TEST_ASSERT_TRUE(first.lastGameResult(last));
    TEST_ASSERT_EQUAL(1, last.playerAScore);
    TEST_ASSERT_EQUAL(11, last.playerBScore);
    TEST_ASSERT_EQUAL(anna.getId(), last.winnerPlayerId);
    TEST_ASSERT_EQUAL(1, f.tournament.listedCount());
    expectEntry({KRYS, ANNA, 1, 11, 1}, entry(f.tournament, 0), "KRYSTIAN - ANNA");
}

// SET_SIDE after a finished game: the finished game stays, the next game's pair changes.
void test_reassign_after_game() {
    Fixture f(new SquashRules(), true);
    Match &first = f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS));
    play(first, GameSide::a, 11, 4);
    expectAck(AckStatus::Applied, f.tournament.reassignSide(0, OLA), "next pair");
    Match *next = f.tournament.getActiveMatch();
    TEST_ASSERT_NOT_EQUAL(&first, next);
    TEST_ASSERT_FALSE(next->hasGame());
    TEST_ASSERT_EQUAL(1, first.getMatchResult().scoreOf(f.p(ANNA).getId()));
    TEST_ASSERT_EQUAL_PTR(&f.p(OLA), &next->getLeftCourtSidePlayer());
    TEST_ASSERT_EQUAL_PTR(&f.p(KRYS), &next->getRightCourtSidePlayer());
}

// A peer's edit changes the standing; GAME_OVER keeps what was played.
void test_peer_edit_and_game_over() {
    Fixture f(new SquashRules());
    Match &match = f.tournament.chooseMatchBetween(f.p(ANNA), f.p(KRYS));
    play(match, GameSide::a, 11, 4);
    GameRecord r = f.tournament.listedRecord(0);
    expectMerge(Merge::Replaced, f.tournament.mergeRecord(image(r.id, 1, r.order, ANNA, KRYS, 4, 11).b, T1), "edit");
    TEST_ASSERT_EQUAL(1, match.getMatchResult().scoreOf(f.p(KRYS).getId()));
    TEST_ASSERT_EQUAL(0, match.getMatchResult().scoreOf(f.p(ANNA).getId()));
    GameResult last = {};
    TEST_ASSERT_TRUE(match.lastGameResult(last));
    TEST_ASSERT_EQUAL(11, last.playerAScore);
    // A peer's delete of a board game.
    expectMerge(Merge::Replaced, f.tournament.mergeRecord(image(r.id, 2, r.order, ANNA, KRYS, 4, 11, 1).b, T1), "del");
    TEST_ASSERT_EQUAL(0, match.getMatchResult().scoreOf(f.p(KRYS).getId()));
    TEST_ASSERT_EQUAL(0, f.tournament.listedCount());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_log_order_and_turns);
    RUN_TEST(test_abandoned_game_not_listed);
    RUN_TEST(test_padel_gems);
    RUN_TEST(test_id_and_epoch);
    RUN_TEST(test_final_scores);
    RUN_TEST(test_add_invalid);
    RUN_TEST(test_add_without_match);
    RUN_TEST(test_add_swapped_pair);
    RUN_TEST(test_live_game_after_add);
    RUN_TEST(test_last_game_result_none);
    RUN_TEST(test_remove_renumbers_and_bumps_rev);
    RUN_TEST(test_remove_invalid);
    RUN_TEST(test_offline_only_sides_not_swapped);
    RUN_TEST(test_listed_cap);
    RUN_TEST(test_game_ids);
    RUN_TEST(test_sync_join_and_begin);
    RUN_TEST(test_sync_pull);
    RUN_TEST(test_sync_merge);
    RUN_TEST(test_sync_tombstone);
    RUN_TEST(test_merge_drops);
    RUN_TEST(test_merge_serial_rev);
    RUN_TEST(test_sort_and_rev);
    RUN_TEST(test_tombstone_eviction);
    RUN_TEST(test_cap_reservation);
    RUN_TEST(test_full_log);
    RUN_TEST(test_ack_guard);
    RUN_TEST(test_reset_writer);
    RUN_TEST(test_roster_growth);
    RUN_TEST(test_leave);
    RUN_TEST(test_reassign_running_game);
    RUN_TEST(test_reassign_after_game);
    RUN_TEST(test_peer_edit_and_game_over);
    return UNITY_END();
}
