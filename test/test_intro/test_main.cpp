// LedIntroAnimation, the walk-on wipe before every game. A fill, not a ring: it
// starts at the outer edge, never un-lights a slot, and from the end of the wipe
// through the hold paints every live front slot in its half's colour (the x == 0
// seam, V1's colon, stays dark). V1's bar then fades out nearest-first. Both boards.
#include <cmath>
#include <functional>

#include <unity.h>

#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Animation/LedIntroAnimation.h"
#include "../common/board_config.h"
#include "../common/check.h"
#include "../common/host_globals.h"

static const CRGB OFF(0, 0, 0);
static const CRGB SENTINEL(1, 2, 3);
static const CRGB LEFT_COLOR(255, 0, 40);
static const CRGB RIGHT_COLOR(0, 90, 255);

void setUp() {}
void tearDown() {}

static bool isSkip(const int slot) { return LedSlots::POS[slot][0] == LedSlots::SKIP; }
static bool isSeam(const int slot) { return !isSkip(slot) && LedSlots::POS[slot][0] == 0; }

static float distance(const int slot) {
    const float x = static_cast<float>(LedSlots::POS[slot][0]);
    const float y = static_cast<float>(LedSlots::POS[slot][1]);
    return std::sqrt(x * x + y * y);
}

/**
 * The standalone animation over the real elementMap (never a hand-kept slot list):
 * outgoing = Bar (empty on V2), colour override BorderTop -> left, BorderBottom ->
 * right (empty on V1).
 */
struct Intro {
    CRGB scratch[Board::LED_COUNT];
    uint16_t elementMap[Board::LED_COUNT];
    LedIntroAnimation intro;
    const LedIntroAnimation::Params p = LedIntroAnimation::defaults();
    uint32_t total = 0;
    uint32_t holdEnd = 0;
    CRGB holdFrame[Board::LED_COUNT];

    Intro() {
        LedDisplay source(scratch);
        for (int i = 0; i < Board::LED_COUNT; i++) elementMap[i] = source.elementsAt(i);
        intro.start(0, LEFT_COLOR, RIGHT_COLOR, elementMap, LedTarget::Bar, LedTarget::BorderTop, LedTarget::BorderBottom);
        total = intro.totalMs();
        holdEnd = static_cast<uint32_t>(p.wipeMs) + p.holdMs;
        intro.renderFrame(holdEnd, holdFrame);
    }

    bool isOutgoing(const int i) const { return (elementMap[i] & LedTarget::Bar) != 0; }

    CRGB sideColor(const int i) const {
        if (elementMap[i] & LedTarget::BorderTop) return LEFT_COLOR;
        if (elementMap[i] & LedTarget::BorderBottom) return RIGHT_COLOR;
        return LedSlots::POS[i][0] < 0 ? LEFT_COLOR : RIGHT_COLOR;
    }

    /** Frames every 10 ms over a sentinel fill; prev holds each slot's last painted colour (or SENTINEL). */
    void forEachFrame(const std::function<void(uint32_t t, const CRGB *out, const CRGB *prev)> &visit) {
        CRGB prev[Board::LED_COUNT];
        fillPixels(prev, Board::LED_COUNT, SENTINEL);
        for (uint32_t t = 0; t <= total; t += 10) {
            CRGB out[Board::LED_COUNT];
            fillPixels(out, Board::LED_COUNT, SENTINEL);
            intro.renderFrame(t, out);
            visit(t, out, prev);
            for (int i = 0; i < Board::LED_COUNT; i++) {
                if (!isSkip(i) && !isSeam(i) && !(out[i] == SENTINEL)) prev[i] = out[i];
            }
        }
    }
};

static void test_skip_slots_match_reserved_list() {
    for (int i = 0; i < Board::LED_COUNT; i++) {
        CHECK(isSkip(i) == isReservedSlot(i), "slot %d SKIP=%d disagrees with the reserved list", i, isSkip(i));
    }
}

static void test_seam_slot_count() {
    int seams = 0;
    for (int i = 0; i < Board::LED_COUNT; i++) seams += isSeam(i);
    TEST_ASSERT_EQUAL_INT(BOARD.seamSlots, seams);
}

static void test_total_duration() {
    Intro f;
    TEST_ASSERT_EQUAL_UINT32(BOARD.introTotalMs, f.total);
}

static void test_active_until_total() {
    Intro f;
    TEST_ASSERT_TRUE_MESSAGE(f.intro.active(f.total - 1), "inactive before wipe+hold+out");
    TEST_ASSERT_FALSE_MESSAGE(f.intro.active(f.total), "still active at wipe+hold+out");
}

static void test_never_writes_skip_or_seam() {
    Intro f;
    f.forEachFrame([](const uint32_t t, const CRGB *out, const CRGB *) {
        for (int i = 0; i < Board::LED_COUNT; i++) {
            CHECK(!((isSkip(i) || isSeam(i)) && !(out[i] == SENTINEL)), "t=%u wrote %s slot %d", t, isSkip(i) ? "SKIP" : "seam", i);
        }
    });
}

static void test_painted_slot_never_goes_dark_again() {
    Intro f;
    f.forEachFrame([](const uint32_t t, const CRGB *out, const CRGB *prev) {
        for (int i = 0; i < Board::LED_COUNT; i++) {
            CHECK(!(out[i] == SENTINEL && !(prev[i] == SENTINEL)), "t=%u slot %d went dark again", t, i);
        }
    });
}

// Monotone only through wipe + hold; the out phase fades outgoing slots on purpose.
static void test_no_slot_dims_through_the_hold() {
    Intro f;
    f.forEachFrame([&f](const uint32_t t, const CRGB *out, const CRGB *prev) {
        if (t > f.holdEnd) return;
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (out[i] == SENTINEL || prev[i] == SENTINEL || isSkip(i) || isSeam(i)) continue;
            CHECK(out[i].r >= prev[i].r && out[i].g >= prev[i].g && out[i].b >= prev[i].b,
                  "t=%u slot %d dimmed from %s to %s", t, i, rgb(prev[i]).c_str(), rgb(out[i]).c_str());
        }
    });
}

static void test_never_brighter_than_side_colour() {
    Intro f;
    f.forEachFrame([&f](const uint32_t t, const CRGB *out, const CRGB *) {
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (out[i] == SENTINEL || isSkip(i) || isSeam(i)) continue;
            const CRGB side = f.sideColor(i);
            CHECK(out[i].r <= side.r && out[i].g <= side.g && out[i].b <= side.b,
                  "t=%u slot %d %s brighter than its side's colour", t, i, rgb(out[i]).c_str());
        }
    });
}

// From the end of the wipe through the hold every live, non-seam slot is painted,
// BorderTop/BorderBottom in their override colour.
static void test_every_slot_in_side_colour_through_the_hold() {
    Intro f;
    f.forEachFrame([&f](const uint32_t t, const CRGB *out, const CRGB *) {
        if (t < f.p.wipeMs || t > f.holdEnd) return;
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (isSkip(i) || isSeam(i)) continue;
            CHECK(!(out[i] == SENTINEL), "t=%u slot %d unpainted after the wipe", t, i);
            CHECK_RGB(f.sideColor(i), out[i], "t=%u slot %d", t, i);
        }
    });
}

// Only outgoing slots change after the hold, and only towards black.
static void test_out_phase_only_fades_outgoing() {
    Intro f;
    f.forEachFrame([&f](const uint32_t t, const CRGB *out, const CRGB *prev) {
        if (t <= f.holdEnd) return;
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (out[i] == SENTINEL || isSkip(i) || isSeam(i)) continue;
            if (f.isOutgoing(i)) {
                CHECK(out[i].r <= prev[i].r && out[i].g <= prev[i].g && out[i].b <= prev[i].b,
                      "t=%u outgoing slot %d brightened in the out phase", t, i);
            } else {
                CHECK_RGB(f.holdFrame[i], out[i], "t=%u slot %d changed during the out phase", t, i);
            }
        }
    });
}

// The first frame that lights anything lights only the outermost ring.
static void test_wipe_starts_at_outer_edge() {
    Intro f;
    float maxRadius = 0.0f;
    for (int i = 0; i < Board::LED_COUNT; i++) {
        if (!isSkip(i) && distance(i) > maxRadius) maxRadius = distance(i);
    }

    bool seen = false;
    f.forEachFrame([&](const uint32_t t, const CRGB *out, const CRGB *) {
        if (seen) return;
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (out[i] == SENTINEL || isSkip(i) || isSeam(i)) continue;
            seen = true;
        }
        if (!seen) return;
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (out[i] == SENTINEL) continue;
            CHECK(distance(i) >= maxRadius - f.p.edge,
                  "t=%u first lit slot %d at r=%.0f, not the outer edge (max %.0f)", t, i, distance(i), maxRadius);
        }
    });
    TEST_ASSERT_TRUE_MESSAGE(seen, "never lit anything");
}

// Every outgoing slot reaches exactly black by the end, nearest-to-centre first.
static void test_outgoing_slots_go_dark_nearest_first() {
    Intro f;
    CRGB finalFrame[Board::LED_COUNT];
    f.intro.renderFrame(f.total, finalFrame);

    int firstDarkAt[Board::LED_COUNT];
    for (int i = 0; i < Board::LED_COUNT; i++) firstDarkAt[i] = -1;
    for (uint32_t t = f.holdEnd; t <= f.total; t += 10) {
        CRGB out[Board::LED_COUNT];
        f.intro.renderFrame(t, out);
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (f.isOutgoing(i) && firstDarkAt[i] < 0 && out[i] == OFF) firstDarkAt[i] = static_cast<int>(t);
        }
    }

    for (int i = 0; i < Board::LED_COUNT; i++) {
        if (!f.isOutgoing(i)) continue;
        CHECK_RGB(OFF, finalFrame[i], "outgoing slot %d at the end", i);
        CHECK(firstDarkAt[i] >= 0, "outgoing slot %d never went dark", i);
    }
    for (int i = 0; i < Board::LED_COUNT; i++) {
        if (!f.isOutgoing(i)) continue;
        for (int j = 0; j < Board::LED_COUNT; j++) {
            if (!f.isOutgoing(j)) continue;
            CHECK(!(distance(i) < distance(j) && firstDarkAt[i] > firstDarkAt[j]),
                  "outgoing slot %d (closer, r=%.0f) went dark after slot %d (farther, r=%.0f)",
                  i, distance(i), j, distance(j));
        }
    }
}

// Visibly animated, not a pop at the end: halfway through, the bar is part gone.
static void test_out_phase_midpoint_is_partial() {
    Intro f;
    if (f.total <= f.holdEnd) return;   // V2: nothing outgoing, zero-length out phase
    CRGB mid[Board::LED_COUNT];
    f.intro.renderFrame(f.holdEnd + (f.total - f.holdEnd) / 2, mid);
    int dark = 0, lit = 0;
    for (int i = 0; i < Board::LED_COUNT; i++) {
        if (!f.isOutgoing(i)) continue;
        if (mid[i] == OFF) dark++;
        else lit++;
    }
    CHECK(dark > 0 && lit > 0, "out phase midpoint: %d outgoing dark, %d lit", dark, lit);
}

// ----------------------------------------------------------- via LedDisplay ---

static const uint32_t START = 7000;

/** The intro view's dark base: glyphs empty, colon and border off, indicators in player colours. */
struct DisplayIntro {
    CRGB buffer[Board::LED_COUNT];
    LedDisplay display{buffer};
    const Color l{LEFT_COLOR.r, LEFT_COLOR.g, LEFT_COLOR.b};
    const Color r{RIGHT_COLOR.r, RIGHT_COLOR.g, RIGHT_COLOR.b};

    void start() {
        display.setSameSideMode(false);
        display.resetAnimations();
        display.setGlyphsGlyph(Glyph::Empty, Glyph::Empty, Glyph::Empty, Glyph::Empty);
        display.setColonAppearance();
        display.setBorderEnabled(false);
        display.setPlayersIndicatorsState(true);
        display.setIndicatorAppearancePlayerA(l);
        display.setIndicatorAppearancePlayerB(r);
        g_fakeMillis = START;
        display.startIntro(l, r);
    }

    void renderAt(const uint32_t ms) {
        fillPixels(buffer, Board::LED_COUNT, OFF);
        g_fakeMillis = ms;
        display.render();
    }
};

// Normal over black, so the front equals the layer alone; indicators keep the base.
static void test_display_front_equals_the_layer() {
    DisplayIntro d;
    uint16_t elementMap[Board::LED_COUNT];
    for (int i = 0; i < Board::LED_COUNT; i++) elementMap[i] = d.display.elementsAt(i);
    d.start();

    LedIntroAnimation reference;
    reference.start(START, LEFT_COLOR, RIGHT_COLOR, elementMap, LedTarget::Bar, LedTarget::BorderTop, LedTarget::BorderBottom);
    const uint32_t total = reference.totalMs();

    for (uint32_t t = 0; t <= total + 20; t += 10) {
        d.renderAt(START + t);

        CRGB expected[Board::LED_COUNT];
        fillPixels(expected, Board::LED_COUNT, OFF);
        reference.render(START + t, expected);
        expected[BOARD.indicatorA] = LEFT_COLOR;
        expected[BOARD.indicatorB] = RIGHT_COLOR;

        for (int i = 0; i < Board::LED_COUNT; i++) CHECK_RGB(expected[i], d.buffer[i], "t=%u slot %d", t, i);
        CHECK(d.display.introActive() == (t < total), "t=%u introActive()=%d", t, d.display.introActive());
    }
}

// Mid-wipe it ends on the very next frame.
static void test_display_intro_stops_on_reset_animations() {
    DisplayIntro d;
    d.start();
    d.renderAt(START + 500);
    d.display.resetAnimations();
    d.renderAt(START + 510);
    for (int i = 0; i < Board::LED_COUNT; i++) {
        if (i == BOARD.indicatorA || i == BOARD.indicatorB) continue;
        CHECK_RGB(OFF, d.buffer[i], "slot %d survived resetAnimations()", i);
    }
    TEST_ASSERT_FALSE_MESSAGE(d.display.introActive(), "still active after resetAnimations()");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_skip_slots_match_reserved_list);
    RUN_TEST(test_seam_slot_count);
    RUN_TEST(test_total_duration);
    RUN_TEST(test_active_until_total);
    RUN_TEST(test_never_writes_skip_or_seam);
    RUN_TEST(test_painted_slot_never_goes_dark_again);
    RUN_TEST(test_no_slot_dims_through_the_hold);
    RUN_TEST(test_never_brighter_than_side_colour);
    RUN_TEST(test_every_slot_in_side_colour_through_the_hold);
    RUN_TEST(test_out_phase_only_fades_outgoing);
    RUN_TEST(test_wipe_starts_at_outer_edge);
    RUN_TEST(test_outgoing_slots_go_dark_nearest_first);
    RUN_TEST(test_out_phase_midpoint_is_partial);
    RUN_TEST(test_display_front_equals_the_layer);
    RUN_TEST(test_display_intro_stops_on_reset_animations);
    return UNITY_END();
}
