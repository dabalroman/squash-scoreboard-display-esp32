// Game ball: Rules::willWinOnNextPointScored for every sport, Game on the committed
// score (uncommitted points ignored), PadelGemScorer::willWinOnNextRally at gem and
// tiebreak level. Board-agnostic.
#include <unity.h>

#include "Tournament/Game/Game.h"
#include "Tournament/Game/PadelGemScorer.h"
#include "Tournament/Rules/PadelRules.h"
#include "Tournament/Rules/ShortVolleyballRules.h"
#include "Tournament/Rules/SquashRules.h"
#include "Tournament/Rules/VolleyballRules.h"
#include "../common/check.h"
#include "../common/host_globals.h"

void setUp() {}
void tearDown() {}

/** Which sides are one point from winning (side a = left). */
struct GameBall {
    bool left, right;

    bool operator==(const GameBall &o) const { return left == o.left && right == o.right; }
    GameBall mirrored() const { return {right, left}; }
};

static const GameBall NOBODY{false, false};
static const GameBall LEFT{true, false};
static const GameBall RIGHT{false, true};
static const GameBall BOTH{true, true};

struct ScoreCase {
    int a, b;
    GameBall want;
};

static GameBall gameBall(const Rules &rules, const int a, const int b) {
    return {rules.willWinOnNextPointScored(a, b, GameSide::a), rules.willWinOnNextPointScored(a, b, GameSide::b)};
}

static GameBall gameBall(const Game &game) {
    return {game.willWinOnNextPointScored(GameSide::a), game.willWinOnNextPointScored(GameSide::b)};
}

static GameBall gameBall(const PadelGemScorer &scorer) {
    return {scorer.willWinOnNextRally(GameSide::a), scorer.willWinOnNextRally(GameSide::b)};
}

static void assertGameBall(const GameBall want, const GameBall got, const std::string &what) {
    CHECK(got == want, "%s: left=%d right=%d, expected left=%d right=%d",
          what.c_str(), got.left, got.right, want.left, want.right);
}

/** Every case, and its mirror (the rules are symmetric); GameSide::none never has game ball. */
template <size_t N>
static void assertRulesTable(const char *sport, const Rules &rules, const ScoreCase (&cases)[N]) {
    for (const ScoreCase &c : cases) {
        assertGameBall(c.want, gameBall(rules, c.a, c.b), strf("%s %d-%d", sport, c.a, c.b));
        assertGameBall(c.want.mirrored(), gameBall(rules, c.b, c.a), strf("%s %d-%d (mirrored)", sport, c.b, c.a));
        CHECK(!rules.willWinOnNextPointScored(c.a, c.b, GameSide::none),
              "%s %d-%d: GameSide::none has game ball", sport, c.a, c.b);
    }
}

/** Committed rallies a-b (a = left). */
static PadelGemScorer padelScorerAt(const bool tiebreak, const int a, const int b) {
    PadelGemScorer scorer;
    scorer.reset(tiebreak);
    // Interleaved, so no intermediate score is a finished gem (winnerOf never latches).
    for (int i = 0; i < a || i < b; i++) {
        if (i < a) scorer.scoreRally(GameSide::a);
        if (i < b) scorer.scoreRally(GameSide::b);
    }
    scorer.commit();
    return scorer;
}

static void test_squash_game_ball() {
    const ScoreCase cases[] = {
        {0, 0, NOBODY}, {10, 9, LEFT}, {10, 0, LEFT}, {10, 10, NOBODY},
        {11, 10, LEFT}, {11, 9, NOBODY}, {12, 10, NOBODY}, {9, 9, NOBODY},
    };
    assertRulesTable("squash", SquashRules(), cases);
}

static void test_volleyball_game_ball() {
    const ScoreCase cases[] = {
        {0, 0, NOBODY}, {24, 23, LEFT}, {24, 24, NOBODY}, {25, 24, LEFT}, {25, 23, NOBODY}, {23, 23, NOBODY},
    };
    assertRulesTable("volleyball", VolleyballRules(), cases);
}

static void test_short_volleyball_game_ball() {
    const ScoreCase cases[] = {
        {0, 0, NOBODY}, {14, 13, LEFT}, {14, 14, NOBODY}, {15, 13, NOBODY},
    };
    assertRulesTable("short volleyball", ShortVolleyballRules(), cases);
}

// At 6-6 the rules alone say both (any score above 6 wins); the padel view never
// asks them there - it breathes on the scorer's rallies.
static void test_padel_gem_level_game_ball() {
    const ScoreCase cases[] = {
        {0, 0, NOBODY}, {5, 4, LEFT}, {5, 3, LEFT}, {5, 5, NOBODY}, {6, 5, LEFT},
        {4, 4, NOBODY}, {6, 4, NOBODY}, {7, 6, NOBODY}, {6, 6, BOTH},
    };
    assertRulesTable("padel gems", PadelRules(), cases);
}

static void test_game_ball_follows_committed_score_only() {
    SquashRules rules;
    Game game(&rules);
    for (int i = 0; i < 10; i++) game.scorePoint(GameSide::a);
    for (int i = 0; i < 9; i++) game.scorePoint(GameSide::b);
    assertGameBall(NOBODY, gameBall(game), "squash 10-9 all uncommitted");

    game.commit();
    assertGameBall(LEFT, gameBall(game), "squash 10-9 committed");

    game.scorePoint(GameSide::b);
    assertGameBall(LEFT, gameBall(game), "squash 10-9, then 10-10 uncommitted");

    game.losePoint(GameSide::b);
    game.scorePoint(GameSide::a);
    assertGameBall(LEFT, gameBall(game), "squash 10-9, then 11-9 uncommitted");
}

// The gem is to 4 (3 = 40), win by 2.
static void test_padel_gem_rallies() {
    const ScoreCase cases[] = {
        {0, 0, NOBODY}, {3, 2, LEFT}, {3, 3, NOBODY}, {4, 3, LEFT}, {3, 4, RIGHT}, {5, 3, NOBODY},
    };
    for (const ScoreCase &c : cases) {
        assertGameBall(c.want, gameBall(padelScorerAt(false, c.a, c.b)), strf("gem rallies %d-%d", c.a, c.b));
    }
}

// The tiebreak is to 7, win by 2, and winning it wins the set.
static void test_padel_tiebreak_rallies() {
    const ScoreCase cases[] = {
        {0, 0, NOBODY}, {6, 5, LEFT}, {6, 6, NOBODY}, {7, 6, LEFT}, {6, 7, RIGHT},
        {8, 6, NOBODY}, {3, 0, NOBODY}, {6, 0, LEFT},
    };
    for (const ScoreCase &c : cases) {
        assertGameBall(c.want, gameBall(padelScorerAt(true, c.a, c.b)), strf("tiebreak rallies %d-%d", c.a, c.b));
    }
}

static void test_padel_uncommitted_rallies_ignored() {
    PadelGemScorer gem = padelScorerAt(false, 3, 0);
    gem.scoreRally(GameSide::b);
    assertGameBall(LEFT, gameBall(gem), "gem 40:00, then 40:15 uncommitted");

    PadelGemScorer tiebreak = padelScorerAt(true, 6, 5);
    tiebreak.scoreRally(GameSide::b);
    assertGameBall(LEFT, gameBall(tiebreak), "tiebreak 6-5, then 6-6 uncommitted");

    tiebreak.undoRally(GameSide::b);
    assertGameBall(LEFT, gameBall(tiebreak), "tiebreak 6-5, uncommitted rally undone");
}

// ------------------------------------------------------------- on fire (#59) ---

static void test_on_fire_streak_threshold() {
    SquashRules rules;
    Game game(&rules);
    for (int i = 0; i < 4; i++) { game.scorePoint(GameSide::a); game.commit(); }
    TEST_ASSERT_FALSE_MESSAGE(game.isOnFire(GameSide::a), "4 committed points: not on fire yet");

    game.scorePoint(GameSide::a);
    game.commit();
    TEST_ASSERT_TRUE_MESSAGE(game.isOnFire(GameSide::a), "5 committed points: on fire");
    TEST_ASSERT_FALSE_MESSAGE(game.isOnFire(GameSide::b), "the other side never scored");
}

static void test_on_fire_ignores_uncommitted_points() {
    SquashRules rules;
    Game game(&rules);
    for (int i = 0; i < 5; i++) game.scorePoint(GameSide::a);   // all uncommitted
    TEST_ASSERT_FALSE_MESSAGE(game.isOnFire(GameSide::a), "5 scored but uncommitted: not on fire yet");

    game.commit();
    TEST_ASSERT_TRUE_MESSAGE(game.isOnFire(GameSide::a), "commit lights the streak");
}

static void test_on_fire_opponent_uncommitted_point_does_not_break_it() {
    SquashRules rules;
    Game game(&rules);
    for (int i = 0; i < 5; i++) game.scorePoint(GameSide::a);
    game.commit();
    TEST_ASSERT_TRUE_MESSAGE(game.isOnFire(GameSide::a), "5-0 committed: on fire");

    game.scorePoint(GameSide::b);   // uncommitted
    TEST_ASSERT_TRUE_MESSAGE(game.isOnFire(GameSide::a), "opponent's uncommitted point must not break the streak");

    game.commit();
    TEST_ASSERT_FALSE_MESSAGE(game.isOnFire(GameSide::a), "opponent's committed point breaks the streak");
}

static void test_on_fire_own_lost_point_counts_until_commit() {
    SquashRules rules;
    Game game(&rules);
    for (int i = 0; i < 5; i++) game.scorePoint(GameSide::a);
    game.commit();
    TEST_ASSERT_TRUE_MESSAGE(game.isOnFire(GameSide::a), "5-0 committed: on fire");

    game.losePoint(GameSide::a);   // marks the last committed point 'lost' (undo pending)
    TEST_ASSERT_TRUE_MESSAGE(game.isOnFire(GameSide::a), "'lost' (uncommitted undo) still counts as committed");

    game.commit();   // erases the lost entry
    TEST_ASSERT_FALSE_MESSAGE(game.isOnFire(GameSide::a), "commit erases the lost point: streak drops to 4");
}

static void test_on_fire_opponents_undone_point_rejoins_streak() {
    SquashRules rules;
    Game game(&rules);
    for (int i = 0; i < 4; i++) game.scorePoint(GameSide::a);
    game.scorePoint(GameSide::b);
    game.commit();   // history: A A A A B, all committed
    TEST_ASSERT_FALSE_MESSAGE(game.isOnFire(GameSide::a), "trailing B breaks the streak");

    game.losePoint(GameSide::b);   // marks B's committed point 'lost'
    game.scorePoint(GameSide::a);  // uncommitted 5th A
    game.commit();                 // erases B (lost), commits the new A -> A A A A A

    TEST_ASSERT_TRUE_MESSAGE(game.isOnFire(GameSide::a), "B undone and committed: the streak re-joins to 5");
}

static void test_on_fire_resets_per_new_game() {
    SquashRules rules;
    Game first(&rules);
    for (int i = 0; i < 5; i++) first.scorePoint(GameSide::a);
    first.commit();
    TEST_ASSERT_TRUE_MESSAGE(first.isOnFire(GameSide::a), "5-0 committed in the first game");

    Game second(&rules);
    TEST_ASSERT_FALSE_MESSAGE(second.isOnFire(GameSide::a), "a fresh game starts unlit - no carry-over");
}

// The engine Game is the set for padel; its history entries are gems, so the
// threshold (3) counts gems won in a row, and a step-back is losePoint+commit.
static void test_padel_on_fire_counts_gems_and_step_back_drops_it() {
    PadelRules rules;
    Game set(&rules);
    set.scorePoint(GameSide::a);
    set.commit();
    set.scorePoint(GameSide::a);
    set.commit();
    TEST_ASSERT_FALSE_MESSAGE(set.isOnFire(GameSide::a), "2 gems: below padel's threshold of 3");

    set.scorePoint(GameSide::a);
    set.commit();
    TEST_ASSERT_TRUE_MESSAGE(set.isOnFire(GameSide::a), "3 gems in a row: on fire");

    set.losePoint(GameSide::a);   // PadelGamePlayingView::stepBackToPreviousGem's undo
    set.commit();
    TEST_ASSERT_FALSE_MESSAGE(set.isOnFire(GameSide::a), "step-back drops the third gem: streak falls to 2");
}

// ------------------------------------------------------------ comeback (#60) ---

static const char *sideName(const GameSide side) {
    return side == GameSide::a ? "a" : side == GameSide::b ? "b" : "none";
}

/**
 * Commits `history` one point per commit ('a'/'b'), so the committed order is
 * exact, discards whatever those commits produced, then scores `addA`/`addB`
 * in one batch and commits again - the case under test. Returns that commit's
 * (still unconsumed) side.
 */
static GameSide comebackAfter(Rules &rules, const char *history, const int addA, const int addB) {
    Game game(&rules);
    for (const char *c = history; *c; c++) {
        game.scorePoint(*c == 'a' ? GameSide::a : GameSide::b);
        game.commit();
    }
    game.takeComebackSide();

    for (int i = 0; i < addA; i++) game.scorePoint(GameSide::a);
    for (int i = 0; i < addB; i++) game.scorePoint(GameSide::b);
    game.commit();
    return game.takeComebackSide();
}

static void assertComeback(const GameSide want, Rules &rules, const char *history, const int addA, const int addB, const char *what) {
    const GameSide got = comebackAfter(rules, history, addA, addB);
    CHECK(got == want, "%s [%s] +%da+%db: got %s, expected %s", what, history, addA, addB, sideName(got), sideName(want));
}

static void test_comeback_fires_when_breaking_a_five_streak() {
    SquashRules squash;
    assertComeback(GameSide::a, squash, "bbbbb", 1, 0, "0:5 -> 1:5");
    assertComeback(GameSide::b, squash, "aaaaa", 0, 1, "5:0 -> 5:1 mirrored");
    assertComeback(GameSide::a, squash, "abbbbbb", 1, 0, "1:6 -> 2:6, streak 6");
}

static void test_comeback_not_on_every_point_while_trailing() {
    SquashRules squash;
    // 9:1 with B's point last: A's streak is 0, so 9:2, 9:3 ... never fire.
    assertComeback(GameSide::none, squash, "aaaaaaaaab", 0, 1, "9:1 -> 9:2");
    assertComeback(GameSide::none, squash, "aaaaaaaaba", 0, 1, "9:1 -> 9:2, A streak 1");
    assertComeback(GameSide::none, squash, "aaaaaaaaabb", 0, 1, "9:2 -> 9:3");
    assertComeback(GameSide::b, squash, "baaaaaaaaa", 0, 1, "9:1 -> 9:2 after A's 9 in a row");
}

static void test_comeback_not_below_threshold() {
    SquashRules squash;
    assertComeback(GameSide::none, squash, "bbbb", 1, 0, "opponent streak 4");
    assertComeback(GameSide::none, squash, "aaaa", 0, 1, "opponent streak 4 mirrored");
}

static void test_comeback_needs_own_positive_delta() {
    SquashRules squash;
    assertComeback(GameSide::none, squash, "bbbbb", 0, 1, "B extends its own streak");
}

static void test_comeback_opponent_uncommitted_points_do_not_count() {
    SquashRules squash;
    // A's 5th point sits in the same batch as B's: before this commit A has 4 committed.
    assertComeback(GameSide::none, squash, "aaaa", 1, 1, "A 4 committed + 1 uncommitted");
}

static void test_comeback_one_per_commit_even_with_multiple_points() {
    SquashRules squash;
    Game game(&squash);
    for (int i = 0; i < 5; i++) {
        game.scorePoint(GameSide::b);
        game.commit();
    }
    game.takeComebackSide();

    game.scorePoint(GameSide::a);
    game.scorePoint(GameSide::a);   // two points in the same batch
    game.commit();
    TEST_ASSERT_TRUE_MESSAGE(game.takeComebackSide() == GameSide::a, "first take: one burst for the whole batch");
    TEST_ASSERT_TRUE_MESSAGE(game.takeComebackSide() == GameSide::none, "second take: consumed, not re-armed");

    game.scorePoint(GameSide::a);
    game.commit();
    TEST_ASSERT_TRUE_MESSAGE(game.takeComebackSide() == GameSide::none, "streak already broken: the next point does not re-fire");
}

static void test_comeback_none_for_a_point_undone_before_commit() {
    SquashRules squash;
    Game game(&squash);
    for (int i = 0; i < 5; i++) {
        game.scorePoint(GameSide::b);
        game.commit();
    }
    game.takeComebackSide();

    game.scorePoint(GameSide::a);
    game.losePoint(GameSide::a);   // undone inside the commit window: net delta 0
    game.commit();
    TEST_ASSERT_TRUE_MESSAGE(game.takeComebackSide() == GameSide::none, "undo-only batch must not fire");
}

static void test_comeback_mixed_batch_checks_side_a_first() {
    SquashRules squash;
    assertComeback(GameSide::a, squash, "bbbbb", 1, 1, "B streak 5, batch +a+b");
}

static void test_comeback_none_on_a_winning_commit() {
    SquashRules squash;
    // 5:10 with A's last 5 in a row; B's 11th point breaks the streak and wins.
    const GameSide got = comebackAfter(squash, "bbbbbbbbbbaaaaa", 0, 1);
    CHECK(got == GameSide::none, "a winning commit must not also fire a burst, got %s", sideName(got));
}

static void test_comeback_volleyball_same_threshold() {
    VolleyballRules volleyball;
    assertComeback(GameSide::b, volleyball, "aaaaa", 0, 1, "volleyball streak 5");
    assertComeback(GameSide::none, volleyball, "aaaa", 0, 1, "volleyball streak 4");
    ShortVolleyballRules shortVolleyball;
    assertComeback(GameSide::a, shortVolleyball, "bbbbb", 1, 0, "short volleyball streak 5");
    assertComeback(GameSide::none, shortVolleyball, "bbbb", 1, 0, "short volleyball streak 4");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_squash_game_ball);
    RUN_TEST(test_volleyball_game_ball);
    RUN_TEST(test_short_volleyball_game_ball);
    RUN_TEST(test_padel_gem_level_game_ball);
    RUN_TEST(test_game_ball_follows_committed_score_only);
    RUN_TEST(test_padel_gem_rallies);
    RUN_TEST(test_padel_tiebreak_rallies);
    RUN_TEST(test_padel_uncommitted_rallies_ignored);
    RUN_TEST(test_on_fire_streak_threshold);
    RUN_TEST(test_on_fire_ignores_uncommitted_points);
    RUN_TEST(test_on_fire_opponent_uncommitted_point_does_not_break_it);
    RUN_TEST(test_on_fire_own_lost_point_counts_until_commit);
    RUN_TEST(test_on_fire_opponents_undone_point_rejoins_streak);
    RUN_TEST(test_on_fire_resets_per_new_game);
    RUN_TEST(test_padel_on_fire_counts_gems_and_step_back_drops_it);
    RUN_TEST(test_comeback_fires_when_breaking_a_five_streak);
    RUN_TEST(test_comeback_not_on_every_point_while_trailing);
    RUN_TEST(test_comeback_not_below_threshold);
    RUN_TEST(test_comeback_needs_own_positive_delta);
    RUN_TEST(test_comeback_opponent_uncommitted_points_do_not_count);
    RUN_TEST(test_comeback_one_per_commit_even_with_multiple_points);
    RUN_TEST(test_comeback_none_for_a_point_undone_before_commit);
    RUN_TEST(test_comeback_mixed_batch_checks_side_a_first);
    RUN_TEST(test_comeback_none_on_a_winning_commit);
    RUN_TEST(test_comeback_volleyball_same_threshold);
    return UNITY_END();
}
