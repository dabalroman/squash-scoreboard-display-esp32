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
    return UNITY_END();
}
