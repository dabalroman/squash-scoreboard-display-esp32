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
#include "Tournament/Rules/SquashRules.h"
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
        WatchSupport::resultEntry(tournament.getFinishedGame(index), out);
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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_log_order_and_turns);
    RUN_TEST(test_abandoned_game_not_listed);
    RUN_TEST(test_padel_gems);
    RUN_TEST(test_id);
    return UNITY_END();
}
