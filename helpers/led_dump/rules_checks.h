// Game-ball rules (task #55): Rules::willWinOnNextPointScored for every sport,
// Game on the committed score (uncommitted points ignored) and PadelGemScorer::willWinOnNextRally
// at gem and tiebreak level. Board-agnostic; shared by check_v1.cpp and check_v2.cpp.
#ifndef LED_DUMP_RULES_CHECKS_H
#define LED_DUMP_RULES_CHECKS_H

#include <cstdio>

#include "Tournament/Game/Game.h"
#include "Tournament/Game/PadelGemScorer.h"
#include "Tournament/Rules/PadelRules.h"
#include "Tournament/Rules/ShortVolleyballRules.h"
#include "Tournament/Rules/SquashRules.h"
#include "Tournament/Rules/VolleyballRules.h"

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

static GameBall gameBall(const Rules &rules, const int a, const int b) {
    return {rules.willWinOnNextPointScored(a, b, GameSide::a), rules.willWinOnNextPointScored(a, b, GameSide::b)};
}

static GameBall gameBall(const Game &game) {
    return {game.willWinOnNextPointScored(GameSide::a), game.willWinOnNextPointScored(GameSide::b)};
}

static GameBall gameBall(const PadelGemScorer &scorer) {
    return {scorer.willWinOnNextRally(GameSide::a), scorer.willWinOnNextRally(GameSide::b)};
}

static int expect(const char *board, const char *what, const GameBall got, const GameBall want) {
    if (got == want) return 0;
    printf("FAIL %s rules %s: left=%d right=%d, expected left=%d right=%d\n",
           board, what, got.left, got.right, want.left, want.right);
    return 1;
}

struct ScoreCase {
    int a, b;
    GameBall want;
};

/** Every case scored by gameBallAt(a, b); mirrored too when the rule is symmetric. */
template <typename GameBallAt>
static int runTable(const char *board, const char *what, const ScoreCase *cases, const int count,
                    const bool mirrored, GameBallAt gameBallAt) {
    int failures = 0;
    for (int i = 0; i < count; i++) {
        const ScoreCase &c = cases[i];
        const GameBall got = gameBallAt(c.a, c.b);
        if (!(got == c.want)) {
            printf("FAIL %s rules %s %d-%d: left=%d right=%d, expected left=%d right=%d\n",
                   board, what, c.a, c.b, got.left, got.right, c.want.left, c.want.right);
            failures++;
        }
        if (mirrored) {
            const ScoreCase swapped{c.b, c.a, c.want.mirrored()};
            failures += runTable(board, what, &swapped, 1, false, gameBallAt);
        }
    }
    return failures;
}

static int runRulesTable(const char *board, const char *sport, const Rules &rules,
                         const ScoreCase *cases, const int count) {
    int failures = runTable(board, sport, cases, count, true,
                            [&](const int a, const int b) { return gameBall(rules, a, b); });
    for (int i = 0; i < count; i++) {
        if (rules.willWinOnNextPointScored(cases[i].a, cases[i].b, GameSide::none)) {
            printf("FAIL %s rules %s %d-%d: GameSide::none has game ball\n", board, sport, cases[i].a, cases[i].b);
            failures++;
        }
    }
    return failures;
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

#define COUNT_OF(x) static_cast<int>(sizeof(x) / sizeof((x)[0]))

static int runRulesChecks(const char *board) {
    int failures = 0;

    const SquashRules squash;
    const ScoreCase squashCases[] = {
        {0, 0, NOBODY}, {10, 9, LEFT}, {10, 0, LEFT}, {10, 10, NOBODY},
        {11, 10, LEFT}, {11, 9, NOBODY}, {12, 10, NOBODY}, {9, 9, NOBODY},
    };
    failures += runRulesTable(board, "squash", squash, squashCases, COUNT_OF(squashCases));

    const VolleyballRules volleyball;
    const ScoreCase volleyballCases[] = {
        {0, 0, NOBODY}, {24, 23, LEFT}, {24, 24, NOBODY}, {25, 24, LEFT}, {25, 23, NOBODY}, {23, 23, NOBODY},
    };
    failures += runRulesTable(board, "volleyball", volleyball, volleyballCases, COUNT_OF(volleyballCases));

    const ShortVolleyballRules shortVolleyball;
    const ScoreCase shortCases[] = {
        {0, 0, NOBODY}, {14, 13, LEFT}, {14, 14, NOBODY}, {15, 13, NOBODY},
    };
    failures += runRulesTable(board, "short volleyball", shortVolleyball, shortCases, COUNT_OF(shortCases));

    // Gem level. At 6-6 the rules alone say both (any score above 6 wins); the
    // padel view never asks them - it breathes on the scorer's rallies.
    const PadelRules padel;
    const ScoreCase padelCases[] = {
        {0, 0, NOBODY}, {5, 4, LEFT}, {5, 3, LEFT}, {5, 5, NOBODY}, {6, 5, LEFT},
        {4, 4, NOBODY}, {6, 4, NOBODY}, {7, 6, NOBODY}, {6, 6, BOTH},
    };
    failures += runRulesTable(board, "padel gems", padel, padelCases, COUNT_OF(padelCases));

    // Game: committed points decide; an uncommitted point neither starts nor stops it.
    {
        SquashRules rules;
        Game game(&rules);
        for (int i = 0; i < 10; i++) game.scorePoint(GameSide::a);
        for (int i = 0; i < 9; i++) game.scorePoint(GameSide::b);
        failures += expect(board, "squash 10-9 all uncommitted", gameBall(game), NOBODY);

        game.commit();
        failures += expect(board, "squash 10-9 committed", gameBall(game), LEFT);

        game.scorePoint(GameSide::b);
        failures += expect(board, "squash 10-9, then 10-10 uncommitted", gameBall(game), LEFT);

        game.losePoint(GameSide::b);
        game.scorePoint(GameSide::a);
        failures += expect(board, "squash 10-9, then 11-9 uncommitted", gameBall(game), LEFT);
    }

    // Rallies, all committed: the gem (to 4, 3 = 40) and the tiebreak (to 7, = the set).
    const ScoreCase gemCases[] = {
        {0, 0, NOBODY}, {3, 2, LEFT}, {3, 3, NOBODY}, {4, 3, LEFT}, {3, 4, RIGHT}, {5, 3, NOBODY},
    };
    const ScoreCase tiebreakCases[] = {
        {0, 0, NOBODY}, {6, 5, LEFT}, {6, 6, NOBODY}, {7, 6, LEFT}, {6, 7, RIGHT},
        {8, 6, NOBODY}, {3, 0, NOBODY}, {6, 0, LEFT},
    };
    failures += runTable(board, "gem rallies", gemCases, COUNT_OF(gemCases), false,
                         [](const int a, const int b) { return gameBall(padelScorerAt(false, a, b)); });
    failures += runTable(board, "tiebreak rallies", tiebreakCases, COUNT_OF(tiebreakCases), false,
                         [](const int a, const int b) { return gameBall(padelScorerAt(true, a, b)); });

    // Uncommitted rallies are ignored: 40:00, then 40:15 still breathes the left.
    {
        PadelGemScorer scorer = padelScorerAt(false, 3, 0);
        scorer.scoreRally(GameSide::b);
        failures += expect(board, "gem 40:00, then 40:15 uncommitted", gameBall(scorer), LEFT);
    }
    {
        PadelGemScorer scorer = padelScorerAt(true, 6, 5);
        scorer.scoreRally(GameSide::b);
        failures += expect(board, "tiebreak 6-5, then 6-6 uncommitted", gameBall(scorer), LEFT);

        scorer.undoRally(GameSide::b);
        failures += expect(board, "tiebreak 6-5, uncommitted rally undone", gameBall(scorer), LEFT);
    }

    printf("rules %s: %d failures\n", board, failures);
    return failures;
}

#undef COUNT_OF

#endif //LED_DUMP_RULES_CHECKS_H
