// Tournament's finished-game log and id (the watch's RESULTS, docs/garmin-protocol.md 19),
// and WatchSupport::resultEntry on top of it. Board-agnostic.
#include <unity.h>

#include <cstring>
#include <memory>

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
    // One commit: `winner` ends on winnerPoints, the other side on loserPoints.
    void play(Match &match, const GameSide winner, const int winnerPoints, const int loserPoints) {
        Game *game = match.createGame();
        const GameSide loser = winner == GameSide::a ? GameSide::b : GameSide::a;
        for (int i = 0; i < loserPoints; i++) game->scorePoint(loser);
        for (int i = 0; i < winnerPoints; i++) game->scorePoint(winner);
        TEST_ASSERT_TRUE(game->commit() == winner);
        match.finishGame();
    }

    struct Entry {
        uint32_t uidA, uidB;
        uint8_t scoreA, scoreB, turn;
    };

    Entry entry(const Tournament &tournament, const size_t index) {
        uint8_t out[Garmin::RESULT_ENTRY_SIZE];
        WatchSupport::resultEntry(tournament, index, out);
        return {Garmin::getU32(out), Garmin::getU32(out + 4), out[8], out[9], out[10]};
    }

    void expectEntry(const Entry want, const Entry got, const char *what) {
        CHECK(want.uidA == got.uidA && want.uidB == got.uidB && want.scoreA == got.scoreA
              && want.scoreB == got.scoreB && want.turn == got.turn,
              "%s: got %08x-%08x %u:%u turn %u, expected %08x-%08x %u:%u turn %u", what,
              static_cast<unsigned>(got.uidA), static_cast<unsigned>(got.uidB), got.scoreA, got.scoreB, got.turn,
              static_cast<unsigned>(want.uidA), static_cast<unsigned>(want.uidB), want.scoreA, want.scoreB, want.turn);
    }

    const uint32_t ANNA = 0x0A0B0C0D;
    const uint32_t KRYS = 0x11223344;
    const uint32_t OLA = 0x55667788;
}

// Chronological across matches, turn per pair, A/B fixed by the pair's first set-up.
void test_log_order_and_turns() {
    UserProfile anna(0, ANNA, "ANNA", Colors::Red);
    UserProfile krys(1, KRYS, "KRYSTIAN", Colors::Blue);
    UserProfile ola(2, OLA, "OLA", Colors::Green);
    Tournament tournament(std::unique_ptr<Rules>(new SquashRules()));
    TEST_ASSERT_EQUAL(0, tournament.getFinishedGameCount());

    play(tournament.chooseMatchBetween(anna, krys), GameSide::a, 11, 7);
    play(tournament.chooseMatchBetween(krys, ola), GameSide::a, 11, 4);
    // The pair again with sides swapped: KRYSTIAN left wins 11:9, still A = ANNA.
    play(tournament.chooseMatchBetween(krys, anna), GameSide::a, 11, 9);

    TEST_ASSERT_EQUAL(3, tournament.getFinishedGameCount());
    expectEntry({ANNA, KRYS, 11, 7, 1}, entry(tournament, 0), "game 0");
    expectEntry({KRYS, OLA, 11, 4, 1}, entry(tournament, 1), "game 1");
    expectEntry({ANNA, KRYS, 9, 11, 2}, entry(tournament, 2), "game 2 (16.13)");
}

// A game left by UNDO at 0:0 is never finished, so it is not listed.
void test_abandoned_game_not_listed() {
    UserProfile anna(0, ANNA, "ANNA", Colors::Red);
    UserProfile krys(1, KRYS, "KRYSTIAN", Colors::Blue);
    Tournament tournament(std::unique_ptr<Rules>(new SquashRules()));
    Match &match = tournament.chooseMatchBetween(anna, krys);

    Game *game = match.createGame();
    game->scorePoint(GameSide::a);
    game->losePoint(GameSide::a);
    TEST_ASSERT_EQUAL(0, tournament.getFinishedGameCount());

    play(match, GameSide::b, 11, 3);
    TEST_ASSERT_EQUAL(1, tournament.getFinishedGameCount());
    expectEntry({ANNA, KRYS, 3, 11, 1}, entry(tournament, 0), "the finished game is turn 1");
}

// Padel: a set's gems, 7-6 after a tiebreak.
void test_padel_gems() {
    UserProfile orly(0, 0x21222324, "ORLY", Colors::Red);
    UserProfile lisy(1, 0x31323334, "LISY", Colors::Blue);
    Tournament tournament(std::unique_ptr<Rules>(new PadelRules()));
    play(tournament.chooseMatchBetween(orly, lisy), GameSide::b, 7, 6);
    expectEntry({0x21222324, 0x31323334, 6, 7, 1}, entry(tournament, 0), "tiebreak set");
}

// Never 0 (that is "no tournament" on the wire), and new per instance.
void test_id() {
    hostRandomState() = 0x12345678u;
    Tournament first(std::unique_ptr<Rules>(new SquashRules()));
    Tournament second(std::unique_ptr<Rules>(new SquashRules()));
    TEST_ASSERT_NOT_EQUAL(0, first.getId());
    TEST_ASSERT_NOT_EQUAL(0, second.getId());
    TEST_ASSERT_NOT_EQUAL(first.getId(), second.getId());

    // The shim's next draw from this state is 0: the id skips it.
    hostRandomState() = 634785765u;
    TEST_ASSERT_EQUAL_HEX32(0, esp_random());
    hostRandomState() = 634785765u;
    Tournament third(std::unique_ptr<Rules>(new SquashRules()));
    TEST_ASSERT_NOT_EQUAL(0, third.getId());
}

// Offline games (ADD_OFFLINE_GAME / REMOVE_OFFLINE_GAME): the model under the commands.
namespace {
    using Garmin::AckStatus;

    struct Listed {
        uint32_t uidA, uidB;
        uint8_t scoreA, scoreB;
        uint16_t turn;
        bool offline;
    };

    Listed listed(const Tournament &tournament, const size_t index) {
        const FinishedGame &entry = tournament.listedGame(index);
        const GameResult &result = entry.match->getGameResult(entry.game);
        return {entry.match->getPlayerA().getUid(), entry.match->getPlayerB().getUid(), result.playerAScore,
                result.playerBScore, tournament.turnOf(index), result.offline};
    }

    void expectListed(const Listed want, const Listed got, const char *what) {
        CHECK(want.uidA == got.uidA && want.uidB == got.uidB && want.scoreA == got.scoreA
              && want.scoreB == got.scoreB && want.turn == got.turn && want.offline == got.offline,
              "%s: got %08x-%08x %u:%u turn %u offline %d, expected %08x-%08x %u:%u turn %u offline %d", what,
              static_cast<unsigned>(got.uidA), static_cast<unsigned>(got.uidB), got.scoreA, got.scoreB, got.turn,
              got.offline, static_cast<unsigned>(want.uidA), static_cast<unsigned>(want.uidB), want.scoreA,
              want.scoreB, want.turn, want.offline);
    }

    void expectAck(const AckStatus want, const AckStatus got, const char *what) {
        CHECK(want == got, "%s: ack %u, expected %u", what, static_cast<unsigned>(got), static_cast<unsigned>(want));
    }

    // ANNA and KRYSTIAN selected, OLA not.
    struct Fixture {
        UserProfile anna{0, ANNA, "ANNA", Colors::Red};
        UserProfile krys{1, KRYS, "KRYSTIAN", Colors::Blue};
        UserProfile ola{2, OLA, "OLA", Colors::Green};
        Tournament tournament;

        explicit Fixture(Rules *rules) : tournament(std::unique_ptr<Rules>(rules)) {
            tournament.addPlayer(anna);
            tournament.addPlayer(krys);
        }
    };

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

// Validation: two distinct selected players and a legal final score.
void test_add_invalid() {
    Fixture f(new SquashRules());
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, ANNA, 11, 3), "uidA == uidB");
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, OLA, 11, 3), "OLA not selected");
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(0x99999999, KRYS, 11, 3), "unknown uid");
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, KRYS, 15, 3), "15:3 squash");
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, KRYS, 10, 8), "unfinished");
    TEST_ASSERT_EQUAL(0, f.tournament.listedCount());
    TEST_ASSERT_EQUAL(0, f.tournament.getMatchCount());

    Fixture padel(new PadelRules());
    expectAck(AckStatus::Invalid, padel.tournament.addOfflineGame(ANNA, KRYS, 8, 6), "padel 8:6");
    expectAck(AckStatus::Invalid, padel.tournament.addOfflineGame(ANNA, KRYS, 7, 0), "padel 7:0");
    expectAck(AckStatus::Applied, padel.tournament.addOfflineGame(ANNA, KRYS, 7, 6), "padel 7:6");
    expectAck(AckStatus::Applied, padel.tournament.addOfflineGame(KRYS, ANNA, 7, 5), "padel 7:5");
    TEST_ASSERT_EQUAL(2, padel.tournament.listedCount());
}

// A new pair gets its match, but the board's active match stays as it was.
void test_add_creates_match_without_active() {
    Fixture f(new SquashRules());
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(KRYS, ANNA, 12, 10), "add");
    TEST_ASSERT_NULL(f.tournament.getActiveMatch());
    TEST_ASSERT_EQUAL(1, f.tournament.getMatchCount());
    TEST_ASSERT_EQUAL(0, f.tournament.getRev());
    expectListed({KRYS, ANNA, 12, 10, 1, true}, listed(f.tournament, 0), "the add sets A/B");

    Match &active = f.tournament.chooseMatchBetween(f.anna, f.krys);
    TEST_ASSERT_EQUAL(1, f.tournament.getMatchCount());
    TEST_ASSERT_EQUAL_PTR(&active, f.tournament.listedGame(0).match);
}

// Either order: the scores land in the match's own A/B order.
void test_add_swapped_pair() {
    Fixture f(new SquashRules());
    play(f.tournament.chooseMatchBetween(f.anna, f.krys), GameSide::a, 11, 7);
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(KRYS, ANNA, 11, 5), "swapped add");
    TEST_ASSERT_EQUAL(1, f.tournament.getMatchCount());
    expectListed({ANNA, KRYS, 11, 7, 1, false}, listed(f.tournament, 0), "tracked");
    expectListed({ANNA, KRYS, 5, 11, 2, true}, listed(f.tournament, 1), "offline, swapped into A/B");

    const MatchResult standing = f.tournament.getActiveMatch()->getMatchResult();
    TEST_ASSERT_EQUAL(1, standing.scoreOf(f.anna.getId()));
    TEST_ASSERT_EQUAL(1, standing.scoreOf(f.krys.getId()));
}

// A game on court while an ADD lands finishes after it; GAME_OVER still shows the played game.
void test_live_game_after_add() {
    Fixture f(new SquashRules());
    Match &match = f.tournament.chooseMatchBetween(f.anna, f.krys);
    Game *game = match.createGame();
    for (int i = 0; i < 11; i++) game->scorePoint(GameSide::b);
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 3), "add mid-game");
    TEST_ASSERT_TRUE(game->commit() == GameSide::b);
    match.finishGame();

    expectListed({ANNA, KRYS, 11, 3, 1, true}, listed(f.tournament, 0), "offline");
    expectListed({ANNA, KRYS, 0, 11, 2, false}, listed(f.tournament, 1), "tracked, next turn");

    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(KRYS, ANNA, 11, 4), "add on GAME_OVER");
    GameResult last = {};
    TEST_ASSERT_TRUE(match.lastGameResult(last));
    TEST_ASSERT_EQUAL(0, last.playerAScore);
    TEST_ASSERT_EQUAL(11, last.playerBScore);
    TEST_ASSERT_FALSE(last.offline);
}

// Offline games alone give a GAME_OVER view nothing to show.
void test_last_game_result_none() {
    Fixture f(new SquashRules());
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 3), "add");
    GameResult last = {};
    TEST_ASSERT_FALSE(f.tournament.chooseMatchBetween(f.anna, f.krys).lastGameResult(last));
}

// Remove: tombstone, the pair's later turns renumbered, rev bumped, other pairs untouched.
void test_remove_renumbers_and_bumps_rev() {
    Fixture f(new SquashRules());
    f.tournament.addPlayer(f.ola);
    play(f.tournament.chooseMatchBetween(f.anna, f.krys), GameSide::a, 11, 7);
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 2), "add");
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(KRYS, OLA, 11, 6), "other pair");
    play(f.tournament.chooseMatchBetween(f.krys, f.anna), GameSide::a, 11, 9);
    TEST_ASSERT_EQUAL(4, f.tournament.listedCount());
    expectListed({ANNA, KRYS, 9, 11, 3, false}, listed(f.tournament, 3), "before remove");

    const uint32_t tid = f.tournament.getId();
    expectAck(AckStatus::Applied, f.tournament.removeOfflineGame(tid, 0, 1), "remove");
    TEST_ASSERT_EQUAL(1, f.tournament.getRev());
    TEST_ASSERT_EQUAL(3, f.tournament.listedCount());
    TEST_ASSERT_EQUAL(4, f.tournament.getFinishedGameCount());
    expectListed({ANNA, KRYS, 11, 7, 1, false}, listed(f.tournament, 0), "tracked 1");
    expectListed({KRYS, OLA, 11, 6, 1, true}, listed(f.tournament, 1), "other pair");
    expectListed({ANNA, KRYS, 9, 11, 2, false}, listed(f.tournament, 2), "renumbered");

    const MatchResult standing = f.tournament.getActiveMatch()->getMatchResult();
    TEST_ASSERT_EQUAL(1, standing.scoreOf(f.anna.getId()));
    TEST_ASSERT_EQUAL(1, standing.scoreOf(f.krys.getId()));

    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid, 0, 1), "stale rev");
    expectAck(AckStatus::Applied, f.tournament.removeOfflineGame(tid, 1, 1), "remove other pair");
    TEST_ASSERT_EQUAL(2, f.tournament.getRev());
    TEST_ASSERT_EQUAL(2, f.tournament.listedCount());
}

void test_remove_invalid() {
    Fixture f(new SquashRules());
    play(f.tournament.chooseMatchBetween(f.anna, f.krys), GameSide::a, 11, 7);
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 2), "add");
    const uint32_t tid = f.tournament.getId();

    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid, 0, 0), "tracked");
    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid + 1, 0, 1), "stale tournament");
    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid, 1, 1), "rev ahead");
    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid, 0, 2), "index == count");
    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid, 0, 0xFFFF), "index far out");
    TEST_ASSERT_EQUAL(0, f.tournament.getRev());
    TEST_ASSERT_EQUAL(2, f.tournament.listedCount());

    expectAck(AckStatus::Applied, f.tournament.removeOfflineGame(tid, 0, 1), "offline");
    expectAck(AckStatus::Invalid, f.tournament.removeOfflineGame(tid, 1, 1), "now past the end");
}

// SIDES_SWAPPED keys on the first tracked game; offline games have no court sides.
void test_offline_only_sides_not_swapped() {
    Fixture f(new SquashRules());
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 3), "add");
    Match &match = f.tournament.chooseMatchBetween(f.krys, f.anna);
    TEST_ASSERT_TRUE(match.getPlayersSwappedCourtSides());
    TEST_ASSERT_FALSE(match.sidesSwapped());

    play(match, GameSide::a, 11, 5);
    TEST_ASSERT_FALSE(match.sidesSwapped());
    f.tournament.chooseMatchBetween(f.anna, f.krys);
    TEST_ASSERT_TRUE(match.sidesSwapped());
}

// Never more listed games than a RESULTS turn holds; a removal frees a slot.
void test_listed_cap() {
    Fixture f(new SquashRules());
    for (size_t i = 0; i < Tournament::MAX_LISTED; i++) {
        expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 0), "fill");
    }
    TEST_ASSERT_EQUAL(128, f.tournament.listedCount());
    TEST_ASSERT_EQUAL(128, f.tournament.turnOf(127));
    expectAck(AckStatus::Invalid, f.tournament.addOfflineGame(ANNA, KRYS, 11, 0), "129th");

    expectAck(AckStatus::Applied, f.tournament.removeOfflineGame(f.tournament.getId(), 0, 0), "remove");
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 0), "slot freed");
    TEST_ASSERT_EQUAL(128, f.tournament.listedCount());
}

// Every stored game, tracked and offline: non-zero and unique, redrawn on a clash.
void test_game_ids() {
    Fixture f(new SquashRules());
    const uint32_t before = hostRandomState();
    play(f.tournament.chooseMatchBetween(f.anna, f.krys), GameSide::a, 11, 7);
    // The next draw repeats the tracked game's id: the add must skip it.
    hostRandomState() = before;
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 2), "add after a clash");
    // Draws 0 next (test_id): skipped too.
    hostRandomState() = 634785765u;
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, KRYS, 11, 4), "add after a 0");

    const Match &match = *f.tournament.getActiveMatch();
    TEST_ASSERT_EQUAL(3, match.getGameCount());
    for (size_t i = 0; i < 3; i++) {
        TEST_ASSERT_NOT_EQUAL(0, match.getGameResult(i).id);
        for (size_t j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(match.getGameResult(j).id, match.getGameResult(i).id);
        }
    }
}

// Tablet mode (spec 20): outbox, ACK, mirror, SET_SIDE.
namespace {
    const uint32_t TID = 0xC0FFEE01;

    // ANNA, KRYSTIAN, OLA all in the pushed roster.
    struct TabletFixture {
        UserProfile anna{0, ANNA, "ANNA", Colors::Red};
        UserProfile krys{1, KRYS, "KRYSTIAN", Colors::Blue};
        UserProfile ola{2, OLA, "OLA", Colors::Green};
        Tournament tournament;

        explicit TabletFixture(Rules *rules = new SquashRules()) : tournament(std::unique_ptr<Rules>(rules), TID) {
            tournament.addPlayer(anna);
            tournament.addPlayer(krys);
            tournament.addPlayer(ola);
        }
    };

    struct Mirror {
        uint8_t count = 0;
        uint8_t bytes[Garmin::RESULTS_MAX * Garmin::GAME_ENTRY_SIZE] = {};

        Mirror &add(const uint32_t id, const uint32_t uidA, const uint32_t uidB, const uint8_t a, const uint8_t b,
                    const uint8_t flags = 0) {
            Garmin::buildGameEntry({id, uidA, uidB, a, b, flags}, bytes + static_cast<size_t>(count) * Garmin::GAME_ENTRY_SIZE);
            count++;
            return *this;
        }

        uint32_t crc() const {
            return Garmin::mirrorCrc(count, bytes);
        }

        AckStatus apply(Tournament &t) const {
            return t.applyMirror(TID, count, crc(), bytes);
        }
    };

    const uint8_t PENDING = static_cast<uint8_t>(Garmin::GameFlags::Pending);
    const uint8_t OFFLINE = static_cast<uint8_t>(Garmin::GameFlags::Offline);

    Garmin::GameEntry outbox(const Tournament &t, const uint16_t index, uint16_t &seq) {
        uint8_t out[Garmin::GAME_ENTRY_SIZE];
        TEST_ASSERT_TRUE(t.outboxEntry(index, seq, out));
        return Garmin::parseGameEntry(out);
    }

    // The id of the board's latest game.
    uint32_t lastId(const Tournament &t) {
        const FinishedGame &f = t.getFinishedGame(t.getFinishedGameCount() - 1);
        return f.match->getGameResult(f.game).id;
    }

    // Everything a failed command must leave alone.
    struct Snapshot {
        uint16_t rev, from, to, outboxCount;
        size_t listed, finished, matches;
        uint32_t mirrorCrc;

        explicit Snapshot(const Tournament &t)
            : rev(t.getRev()), from(t.outboxFrom()), to(t.outboxTo()), outboxCount(t.outboxCount()),
              listed(t.listedCount()), finished(t.getFinishedGameCount()), matches(t.getMatchCount()),
              mirrorCrc(t.getMirrorCrc()) {}

        bool operator==(const Snapshot &o) const {
            return rev == o.rev && from == o.from && to == o.to && outboxCount == o.outboxCount
                   && listed == o.listed && finished == o.finished && matches == o.matches
                   && mirrorCrc == o.mirrorCrc;
        }
    };
}

// The tablet's id; board mode keeps answering WRONG_SCREEN and an all-zero STATUS.
void test_tablet_id_and_board_mode() {
    TabletFixture f;
    TEST_ASSERT_TRUE(f.tournament.isTablet());
    TEST_ASSERT_EQUAL_HEX32(TID, f.tournament.getId());
    const Garmin::TabletStatus s = f.tournament.tabletStatus(1, 3, 0xAABBCCDD);
    TEST_ASSERT_EQUAL_HEX32(TID, s.tournamentId);
    TEST_ASSERT_EQUAL(1, s.outboxFrom);
    TEST_ASSERT_EQUAL(0, s.outboxTo);
    TEST_ASSERT_EQUAL_HEX32(0, s.mirrorCrc);

    Fixture board(new SquashRules());
    TEST_ASSERT_FALSE(board.tournament.isTablet());
    const uint32_t id = board.tournament.getId();
    expectAck(AckStatus::WrongScreen, board.tournament.ackOutbox(id, 0), "board ACK");
    Mirror empty;
    expectAck(AckStatus::WrongScreen, board.tournament.applyMirror(id, 0, empty.crc(), empty.bytes), "board mirror");
    board.tournament.chooseMatchBetween(board.anna, board.krys);
    expectAck(AckStatus::WrongScreen, board.tournament.reassignSide(0, KRYS), "board SET_SIDE");
    TEST_ASSERT_EQUAL(0, board.tournament.tabletStatus(1, 2, 1).tournamentId);

    // Board mode: a tracked game is listed at once and never in an outbox.
    play(*board.tournament.getActiveMatch(), GameSide::a, 11, 3);
    TEST_ASSERT_EQUAL(1, board.tournament.listedCount());
    TEST_ASSERT_EQUAL(0, board.tournament.outboxCount());
    TEST_ASSERT_EQUAL(0, board.tournament.outboxTo());
}

// A tracked game is pending: in the tally, not listed, first in the outbox.
void test_tablet_tracked_pending() {
    TabletFixture f;
    Match &match = f.tournament.chooseMatchBetween(f.krys, f.anna);
    play(match, GameSide::a, 11, 4);

    TEST_ASSERT_EQUAL(0, f.tournament.listedCount());
    TEST_ASSERT_EQUAL(1, f.tournament.getFinishedGameCount());
    TEST_ASSERT_EQUAL(1, match.getMatchResult().scoreOf(f.krys.getId()));
    GameResult last = {};
    TEST_ASSERT_TRUE(match.lastGameResult(last));
    TEST_ASSERT_TRUE(last.pending);

    TEST_ASSERT_EQUAL(1, f.tournament.outboxFrom());
    TEST_ASSERT_EQUAL(1, f.tournament.outboxTo());
    TEST_ASSERT_EQUAL(1, f.tournament.outboxCount());
    uint16_t seq = 0;
    const Garmin::GameEntry g = outbox(f.tournament, 0, seq);
    TEST_ASSERT_EQUAL(1, seq);
    TEST_ASSERT_EQUAL_HEX32(lastId(f.tournament), g.id);
    // The match's own A/B (KRYSTIAN first set it up), never PENDING on the wire.
    TEST_ASSERT_EQUAL_HEX32(KRYS, g.uidA);
    TEST_ASSERT_EQUAL_HEX32(ANNA, g.uidB);
    TEST_ASSERT_EQUAL(11, g.scoreA);
    TEST_ASSERT_EQUAL(4, g.scoreB);
    TEST_ASSERT_EQUAL(0, g.flags);
    uint8_t out[Garmin::GAME_ENTRY_SIZE];
    TEST_ASSERT_FALSE(f.tournament.outboxEntry(1, seq, out));
}

// A watch's offline game: accepted (listed + counted) and in the outbox, flagged OFFLINE.
void test_tablet_offline_listed_in_outbox() {
    TabletFixture f;
    play(f.tournament.chooseMatchBetween(f.anna, f.krys), GameSide::b, 11, 6);
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(OLA, ANNA, 11, 9), "add");
    TEST_ASSERT_EQUAL(1, f.tournament.listedCount());
    expectListed({OLA, ANNA, 11, 9, 1, true}, listed(f.tournament, 0), "offline listed");
    TEST_ASSERT_EQUAL(2, f.tournament.outboxCount());
    TEST_ASSERT_EQUAL(2, f.tournament.outboxTo());
    uint16_t seq = 0;
    const Garmin::GameEntry g = outbox(f.tournament, 1, seq);
    TEST_ASSERT_EQUAL(2, seq);
    TEST_ASSERT_EQUAL(OFFLINE, g.flags);
    // Its pair's turn skips nothing; the pending game of another pair has no turn at all.
    TEST_ASSERT_EQUAL(1, f.tournament.turnOf(0));
}

// ACK: cumulative, a repeat is a no-op, a wrong tid or a seq ahead is INVALID.
void test_tablet_ack() {
    TabletFixture f;
    Match &match = f.tournament.chooseMatchBetween(f.anna, f.krys);
    play(match, GameSide::a, 11, 1);
    play(match, GameSide::a, 11, 2);
    play(match, GameSide::a, 11, 3);

    const Snapshot before(f.tournament);
    expectAck(AckStatus::Invalid, f.tournament.ackOutbox(TID + 1, 1), "other tid");
    expectAck(AckStatus::Invalid, f.tournament.ackOutbox(TID, 4), "up_to > outbox_to");
    TEST_ASSERT_TRUE(Snapshot(f.tournament) == before);

    expectAck(AckStatus::Applied, f.tournament.ackOutbox(TID, 2), "ack 2");
    TEST_ASSERT_EQUAL(3, f.tournament.outboxFrom());
    TEST_ASSERT_EQUAL(3, f.tournament.outboxTo());
    TEST_ASSERT_EQUAL(1, f.tournament.outboxCount());
    uint16_t seq = 0;
    outbox(f.tournament, 0, seq);
    TEST_ASSERT_EQUAL(3, seq);

    expectAck(AckStatus::Applied, f.tournament.ackOutbox(TID, 1), "already acked");
    TEST_ASSERT_EQUAL(3, f.tournament.outboxFrom());
    expectAck(AckStatus::Applied, f.tournament.ackOutbox(TID, 3), "ack all");
    TEST_ASSERT_EQUAL(4, f.tournament.outboxFrom());
    TEST_ASSERT_EQUAL(3, f.tournament.outboxTo());
    TEST_ASSERT_EQUAL(0, f.tournament.outboxCount());
    // An ACK only empties the outbox.
    TEST_ASSERT_EQUAL(3, f.tournament.getFinishedGameCount());
    TEST_ASSERT_EQUAL(3, match.getMatchResult().scoreOf(f.anna.getId()));
    TEST_ASSERT_EQUAL(0, f.tournament.getRev());

    play(match, GameSide::b, 11, 5);
    TEST_ASSERT_EQUAL(4, f.tournament.outboxFrom());
    TEST_ASSERT_EQUAL(4, f.tournament.outboxTo());
}

// The mirror replaces the games; an un-ACKed board game survives after it, an ACKed one the
// mirror lacks is gone, one whose id the mirror carries is ACKed.
void test_tablet_mirror_survivors() {
    TabletFixture f;
    Match &match = f.tournament.chooseMatchBetween(f.anna, f.krys);
    play(match, GameSide::a, 11, 1);
    const uint32_t first = lastId(f.tournament);
    play(match, GameSide::a, 11, 2);
    const uint32_t second = lastId(f.tournament);
    play(match, GameSide::b, 11, 3);
    const uint32_t third = lastId(f.tournament);
    expectAck(AckStatus::Applied, f.tournament.ackOutbox(TID, 1), "ack the first");

    // Another court's accepted game, then the second as a pending card. The first was ACKed
    // and is absent: the tablet deleted it.
    Mirror m;
    m.add(0x0F0F0F0F, KRYS, OLA, 11, 7).add(second, ANNA, KRYS, 11, 2, PENDING);
    expectAck(AckStatus::Applied, m.apply(f.tournament), "mirror");

    TEST_ASSERT_EQUAL(3, f.tournament.getFinishedGameCount());
    TEST_ASSERT_EQUAL(2, f.tournament.getMatchCount());
    TEST_ASSERT_EQUAL_HEX32(m.crc(), f.tournament.getMirrorCrc());
    TEST_ASSERT_EQUAL(1, f.tournament.listedCount());
    expectListed({KRYS, OLA, 11, 7, 1, false}, listed(f.tournament, 0), "other court");
    TEST_ASSERT_EQUAL(1, f.tournament.getRev());

    // Log: mirror order, then the survivor.
    const GameResult &a = f.tournament.getFinishedGame(1).match->getGameResult(f.tournament.getFinishedGame(1).game);
    const GameResult &b = f.tournament.getFinishedGame(2).match->getGameResult(f.tournament.getFinishedGame(2).game);
    TEST_ASSERT_EQUAL_HEX32(second, a.id);
    TEST_ASSERT_TRUE(a.acked && a.local && a.pending);
    TEST_ASSERT_EQUAL(2, a.outboxSeq);
    TEST_ASSERT_EQUAL_HEX32(third, b.id);
    TEST_ASSERT_FALSE(b.acked);
    TEST_ASSERT_NOT_EQUAL(first, a.id);

    TEST_ASSERT_EQUAL(1, f.tournament.outboxCount());
    TEST_ASSERT_EQUAL(3, f.tournament.outboxFrom());
    TEST_ASSERT_EQUAL(3, f.tournament.outboxTo());

    // The court's tally counts the pending card and the un-ACKed game.
    const MatchResult standing = match.getMatchResult();
    TEST_ASSERT_EQUAL(1, standing.scoreOf(f.anna.getId()));
    TEST_ASSERT_EQUAL(1, standing.scoreOf(f.krys.getId()));
    TEST_ASSERT_EQUAL_PTR(&match, f.tournament.getActiveMatch());

    // GAME_OVER's last tracked game is still this board's own, not the mirrored one.
    GameResult last = {};
    TEST_ASSERT_TRUE(match.lastGameResult(last));
    TEST_ASSERT_EQUAL_HEX32(third, last.id);
}

// The tablet accepts the pending card: listed, rev bumps; the same mirror again changes nothing.
void test_tablet_mirror_rev_only_on_listed_change() {
    TabletFixture f;
    Match &match = f.tournament.chooseMatchBetween(f.anna, f.krys);
    play(match, GameSide::a, 11, 1);
    const uint32_t id = lastId(f.tournament);

    Mirror pending;
    pending.add(id, ANNA, KRYS, 11, 1, PENDING);
    expectAck(AckStatus::Applied, pending.apply(f.tournament), "pending card");
    TEST_ASSERT_EQUAL(0, f.tournament.getRev());
    TEST_ASSERT_EQUAL(0, f.tournament.listedCount());
    TEST_ASSERT_EQUAL(0, f.tournament.outboxCount());

    Mirror accepted;
    // The tablet fixed the score and swapped the uid order: still the pair's match, A/B kept.
    accepted.add(id, KRYS, ANNA, 2, 11);
    expectAck(AckStatus::Applied, accepted.apply(f.tournament), "accept");
    TEST_ASSERT_EQUAL(1, f.tournament.getRev());
    expectListed({ANNA, KRYS, 11, 2, 1, false}, listed(f.tournament, 0), "accepted, fixed");
    expectAck(AckStatus::Applied, accepted.apply(f.tournament), "same again");
    TEST_ASSERT_EQUAL(1, f.tournament.getRev());

    Mirror empty;
    expectAck(AckStatus::Applied, empty.apply(f.tournament), "deleted");
    TEST_ASSERT_EQUAL_HEX32(0xD202EF8D, empty.crc());
    TEST_ASSERT_EQUAL(2, f.tournament.getRev());
    TEST_ASSERT_EQUAL(0, f.tournament.getFinishedGameCount());
    TEST_ASSERT_EQUAL(0, match.getMatchResult().scoreOf(f.anna.getId()));
    expectAck(AckStatus::Applied, empty.apply(f.tournament), "empty again");
    TEST_ASSERT_EQUAL(2, f.tournament.getRev());
}

// Every INVALID mirror leaves rev, listing, outbox and crc as they were.
void test_tablet_mirror_invalid() {
    TabletFixture f;
    play(f.tournament.chooseMatchBetween(f.anna, f.krys), GameSide::a, 11, 1);
    expectAck(AckStatus::Applied, f.tournament.addOfflineGame(ANNA, OLA, 11, 5), "add");
    const Snapshot before(f.tournament);

    Mirror good;
    good.add(1, ANNA, KRYS, 11, 1);
    expectAck(AckStatus::Invalid, f.tournament.applyMirror(TID + 1, good.count, good.crc(), good.bytes), "tid");
    expectAck(AckStatus::Invalid, f.tournament.applyMirror(TID, good.count, good.crc() ^ 1, good.bytes), "crc");
    expectAck(AckStatus::Invalid, f.tournament.applyMirror(TID, 2, good.crc(), good.bytes), "count vs crc");

    Mirror cases[6];
    cases[0].add(0, ANNA, KRYS, 11, 1);
    cases[1].add(1, ANNA, ANNA, 11, 1);
    cases[2].add(1, ANNA, 0x99999999, 11, 1);
    cases[3].add(1, ANNA, KRYS, 15, 3);
    cases[4].add(1, ANNA, KRYS, 11, 1).add(1, KRYS, OLA, 11, 1);
    cases[5].add(1, ANNA, KRYS, 11, 1).add(2, KRYS, OLA, 10, 8);
    const char *what[] = {"id 0", "uidA == uidB", "uid not in roster", "15:3", "duplicate id", "unfinished"};
    for (int i = 0; i < 6; i++) expectAck(AckStatus::Invalid, cases[i].apply(f.tournament), what[i]);

    Mirror big;
    for (uint32_t i = 0; i < Garmin::RESULTS_MAX; i++) big.add(i + 1, ANNA, KRYS, 11, 0);
    // 129 entries do not fit the stage; the count alone is refused.
    expectAck(AckStatus::Invalid, f.tournament.applyMirror(TID, 129, 0, big.bytes), "129");
    CHECK(Snapshot(f.tournament) == before, "an INVALID mirror changed the tournament");

    // A full 128-game mirror is fine.
    expectAck(AckStatus::Applied, big.apply(f.tournament), "128");
    TEST_ASSERT_EQUAL(128 + 1, f.tournament.listedCount());
}

// SET_SIDE mid-game: the running Game moves to the new pair, score kept per court side.
void test_tablet_reassign_running_game() {
    TabletFixture f;
    Match &first = f.tournament.chooseMatchBetween(f.krys, f.anna);  // KRYSTIAN left
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
    TEST_ASSERT_EQUAL_PTR(&f.krys, &to->getLeftCourtSidePlayer());
    TEST_ASSERT_EQUAL_PTR(&f.ola, &to->getRightCourtSidePlayer());
    TEST_ASSERT_EQUAL(2, game->getRealScore(GameSide::a));
    TEST_ASSERT_EQUAL(1, game->getRealScore(GameSide::b));

    // The left side to ANNA: a new pair ANNA-OLA.
    expectAck(AckStatus::Applied, f.tournament.reassignSide(0, ANNA), "swap left");
    Match *third = f.tournament.getActiveMatch();
    TEST_ASSERT_EQUAL_PTR(&f.anna, &third->getLeftCourtSidePlayer());
    TEST_ASSERT_EQUAL_PTR(&f.ola, &third->getRightCourtSidePlayer());

    // KRYSTIAN on the right: back to the first pair's match, now swapped (A = KRYSTIAN).
    expectAck(AckStatus::Applied, f.tournament.reassignSide(1, KRYS), "KRYS right");
    TEST_ASSERT_EQUAL_PTR(&first, f.tournament.getActiveMatch());
    TEST_ASSERT_TRUE(first.getPlayersSwappedCourtSides());
    TEST_ASSERT_EQUAL_PTR(&f.anna, &first.getLeftCourtSidePlayer());
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
    TEST_ASSERT_EQUAL(f.anna.getId(), last.winnerPlayerId);
    TEST_ASSERT_EQUAL(0, to->getGameCount());
}

// SET_SIDE after a finished game: the finished game stays, the next game's pair changes.
void test_tablet_reassign_after_game() {
    TabletFixture f;
    Match &first = f.tournament.chooseMatchBetween(f.anna, f.krys);
    play(first, GameSide::a, 11, 4);
    expectAck(AckStatus::Applied, f.tournament.reassignSide(0, OLA), "next pair");
    Match *next = f.tournament.getActiveMatch();
    TEST_ASSERT_NOT_EQUAL(&first, next);
    TEST_ASSERT_FALSE(next->hasGame());
    TEST_ASSERT_EQUAL(1, first.getGameCount());
    TEST_ASSERT_EQUAL(1, first.getMatchResult().scoreOf(f.anna.getId()));
    TEST_ASSERT_EQUAL_PTR(&f.ola, &next->getLeftCourtSidePlayer());
    TEST_ASSERT_EQUAL_PTR(&f.krys, &next->getRightCourtSidePlayer());
}

// The mirror keeps the running Game, activeMatch and the side latches.
void test_tablet_mirror_keeps_running_game() {
    TabletFixture f;
    Match &match = f.tournament.chooseMatchBetween(f.krys, f.anna);
    play(match, GameSide::a, 11, 4);
    f.tournament.chooseMatchBetween(f.anna, f.krys);
    TEST_ASSERT_TRUE(match.sidesSwapped());
    Game *game = match.createGame();
    game->scorePoint(GameSide::b);
    game->commit();

    Mirror m;
    m.add(0x12345678, OLA, ANNA, 11, 0);
    expectAck(AckStatus::Applied, m.apply(f.tournament), "mirror");
    TEST_ASSERT_EQUAL_PTR(&match, f.tournament.getActiveMatch());
    TEST_ASSERT_TRUE(match.hasGame());
    TEST_ASSERT_TRUE(match.sidesSwapped());
    TEST_ASSERT_EQUAL(1, game->getRealScore(GameSide::b));
    // A pair the mirror creates takes uid_a as A.
    expectListed({OLA, ANNA, 11, 0, 1, false}, listed(f.tournament, 0), "new pair");
    // The un-ACKed tracked game survived.
    TEST_ASSERT_EQUAL(1, match.getGameCount());
    TEST_ASSERT_EQUAL(1, f.tournament.outboxCount());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_log_order_and_turns);
    RUN_TEST(test_abandoned_game_not_listed);
    RUN_TEST(test_padel_gems);
    RUN_TEST(test_id);
    RUN_TEST(test_final_scores);
    RUN_TEST(test_add_invalid);
    RUN_TEST(test_add_creates_match_without_active);
    RUN_TEST(test_add_swapped_pair);
    RUN_TEST(test_live_game_after_add);
    RUN_TEST(test_last_game_result_none);
    RUN_TEST(test_remove_renumbers_and_bumps_rev);
    RUN_TEST(test_remove_invalid);
    RUN_TEST(test_offline_only_sides_not_swapped);
    RUN_TEST(test_listed_cap);
    RUN_TEST(test_game_ids);
    RUN_TEST(test_tablet_id_and_board_mode);
    RUN_TEST(test_tablet_tracked_pending);
    RUN_TEST(test_tablet_offline_listed_in_outbox);
    RUN_TEST(test_tablet_ack);
    RUN_TEST(test_tablet_mirror_survivors);
    RUN_TEST(test_tablet_mirror_rev_only_on_listed_change);
    RUN_TEST(test_tablet_mirror_invalid);
    RUN_TEST(test_tablet_reassign_running_game);
    RUN_TEST(test_tablet_reassign_after_game);
    RUN_TEST(test_tablet_mirror_keeps_running_game);
    return UNITY_END();
}
