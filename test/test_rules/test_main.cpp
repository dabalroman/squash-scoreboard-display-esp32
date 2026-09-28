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
    return UNITY_END();
}
