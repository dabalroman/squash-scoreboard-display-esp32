// LedSweepAnimation: the boot sweep through LedDisplay::renderBootFrame, and the
// celebration as layer 2 over the GameOver screen - off the ring every slot equals
// a celebration-free reference frame, on it the blend of the two. Both boards.
#include <functional>

#include <unity.h>

#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Animation/LedSweepAnimation.h"
#include "../common/board_config.h"
#include "../common/check.h"
#include "../common/host_globals.h"

static const CRGB OFF(0, 0, 0);

void setUp() {}
void tearDown() {}

static bool isSwept(const int slot) {
    return slot >= 0 && slot < BOARD.ledCount && !isReservedSlot(slot);
}

static void test_board_led_count() {
    TEST_ASSERT_EQUAL_INT(BOARD.ledCount, Board::LED_COUNT);
}

// ---------------------------------------------------------------------- boot ---

/** Every boot frame from t = 0 to 50 ms past the end, 5 ms apart. */
static void forEachBootFrame(const std::function<void(uint32_t t, uint32_t duration, const CRGB *frame)> &visit) {
    const uint32_t duration = LedSweepAnimation::bootParams().durationMs;
    CRGB buffer[Board::LED_COUNT];
    LedDisplay display(buffer);
    for (uint32_t t = 0; t <= duration + 50; t += 5) {
        display.renderBootFrame(t);
        visit(t, duration, buffer);
    }
}

static void test_boot_sweep_lights_only_swept_slots() {
    forEachBootFrame([](const uint32_t t, uint32_t, const CRGB *frame) {
        for (int i = 0; i < Board::LED_COUNT; i++) {
            CHECK(frame[i] == OFF || isSwept(i), "t=%u lit reserved slot %d", t, i);
        }
    });
}

// The band is sized so the ring always covers at least one LED: the field has
// radial gaps a thinner band would fall into, blanking the strip mid-animation.
static void test_boot_sweep_never_blanks_a_frame() {
    forEachBootFrame([](const uint32_t t, const uint32_t duration, const CRGB *frame) {
        if (t >= duration) return;
        int lit = 0;
        for (int i = 0; i < Board::LED_COUNT; i++) lit += !(frame[i] == OFF);
        CHECK(lit > 0, "t=%u lit nothing", t);
    });
}

static void test_boot_sweep_ends_dark() {
    forEachBootFrame([](const uint32_t t, const uint32_t duration, const CRGB *frame) {
        if (t < duration) return;
        for (int i = 0; i < Board::LED_COUNT; i++) CHECK(frame[i] == OFF, "t=%u slot %d still lit after the sweep", t, i);
    });
}

// From the origin (near) out to both far corners, not just some middle radius.
static void test_boot_sweep_reaches_near_and_both_far_ends() {
    bool near = false, farA = false, farB = false;
    forEachBootFrame([&](uint32_t, uint32_t, const CRGB *frame) {
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (frame[i] == OFF) continue;
            near = near || BOARD.sweepNear.contains(i);
            farA = farA || BOARD.sweepFarA.contains(i);
            farB = farB || BOARD.sweepFarB.contains(i);
        }
    });
    TEST_ASSERT_TRUE_MESSAGE(near, "never reached the near slots");
    TEST_ASSERT_TRUE_MESSAGE(farA, "never reached far end A");
    TEST_ASSERT_TRUE_MESSAGE(farB, "never reached far end B");
}

// --------------------------------------------------------------- celebration ---

static const CRGB RED(255, 0, 0);
static const Color WIN(0, 255, 0);
static const uint32_t START = 5000;

static void celebrationScreen(LedDisplay &display) {
    display.setSameSideMode(false);
    display.setNumericValue(11, 7);
    display.setGlyphsAppearance(Colors::Blue, Colors::Blue);
    display.setColonAppearance(Colors::Blue);
    display.setBorderEnabled(true);
    display.setBorderAppearance(Colors::Blue, Colors::Blue);
    display.setPlayersIndicatorsState(true);
    display.setIndicatorAppearancePlayerA(Colors::Red);
    display.setIndicatorAppearancePlayerB(Colors::Red);
#if BOARD_REV == 1
    display.setLedBarState([] {
        std::array<LedBarPixel, LedBar::PIXEL_COUNT> bar;
        for (uint8_t i = 0; i < LedBar::PIXEL_COUNT; i += 3) bar[i].color = CRGB(0, 0, 255);
        return bar;
    });
#endif
}

/**
 * The celebrating display, a celebration-free reference and the bare ring from a
 * standalone sweep with the same params and origin, rendered at the same instant.
 */
struct Celebration {
    CRGB buffer[Board::LED_COUNT];
    CRGB reference[Board::LED_COUNT];
    CRGB ring[Board::LED_COUNT];
    LedDisplay display{buffer};
    LedDisplay plain{reference};
    LedSweepAnimation::Params params = LedSweepAnimation::celebrationParams();
    LedSweepAnimation ringSweep{ringParams(params)};
    const bool leftWon;

    static LedSweepAnimation::Params ringParams(LedSweepAnimation::Params p) {
        p.solid = CRGB(0, 255, 0);
        return p;
    }

    Celebration(const BlendMode mode, const bool leftWon) : leftWon(leftWon) {
        celebrationScreen(display);
        celebrationScreen(plain);
        ringSweep.setOriginToHalf(leftWon);
        ringSweep.start(START);
        display.setCelebrationBlend(mode);
        g_fakeMillis = START;
        display.startCelebration(WIN, leftWon);
    }

    uint32_t cycle() const { return static_cast<uint32_t>(params.durationMs) + params.gapMs; }
    uint32_t total() const { return cycle() * params.repeats - params.gapMs; }

    void renderAt(const uint32_t ms) {
        fillPixels(buffer, Board::LED_COUNT, OFF);
        fillPixels(reference, Board::LED_COUNT, OFF);
        fillPixels(ring, Board::LED_COUNT, OFF);
        g_fakeMillis = ms;
        display.render();
        plain.render();
        ringSweep.render(ms, ring);
    }

    void assertEqualsReference(const char *when) const {
        for (int i = 0; i < Board::LED_COUNT; i++) CHECK_RGB(reference[i], buffer[i], "%s, slot %d", when, i);
    }
};

static void assertCelebrationSide(const BlendMode mode, const bool leftWon) {
    Celebration c(mode, leftWon);
    const char *name = blendName(mode);
    const char *side = leftWon ? "left won" : "right won";

    for (uint32_t t = 0; t < c.total(); t += 10) {
        c.renderAt(START + t);
        const bool inGap = t % c.cycle() >= c.params.durationMs;
        int lit = 0;

        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (!(c.ring[i] == OFF)) {
                lit++;
                CHECK(isSwept(i) && !inGap, "%s %s t=%u: ring on slot %d (gap=%d)", name, side, t, i, inGap);
                CHECK(c.ring[i].r == 0 && c.ring[i].b == 0 && c.ring[i].g != 0,
                      "%s %s t=%u: ring slot %d not solid green", name, side, t, i);
                CHECK(c.buffer[i].g != 0, "%s %s t=%u: ring invisible on slot %d", name, side, t, i);
            }

            CHECK_RGB(blendPixel(c.reference[i], c.ring[i], mode), c.buffer[i], "%s %s t=%u slot %d", name, side, t, i);
            if (mode != BlendMode::Normal) {
                CHECK(c.buffer[i].r >= c.reference[i].r && c.buffer[i].g >= c.reference[i].g && c.buffer[i].b >= c.reference[i].b,
                      "%s %s t=%u: slot %d darker than the base", name, side, t, i);
            }
        }

        // Indicators face the players and stay exactly as the base drew them.
        CHECK_RGB(RED, c.buffer[BOARD.indicatorA], "%s %s t=%u indicator A", name, side, t);
        CHECK_RGB(RED, c.buffer[BOARD.indicatorB], "%s %s t=%u indicator B", name, side, t);
        CHECK(lit > 0 || inGap, "%s %s t=%u: ring lit nothing", name, side, t);
    }

    // Past the last cycle the frame is the plain screen, byte for byte.
    c.renderAt(START + c.total());
    c.assertEqualsReference(strf("%s %s after %u ms", name, side, c.total()).c_str());
}

static void assertCelebrationBlend(const BlendMode mode) {
    assertCelebrationSide(mode, true);
    assertCelebrationSide(mode, false);
}

static void test_celebration_normal_blend() { assertCelebrationBlend(BlendMode::Normal); }
static void test_celebration_screen_blend() { assertCelebrationBlend(BlendMode::Screen); }
static void test_celebration_add_blend() { assertCelebrationBlend(BlendMode::Add); }
static void test_celebration_lighten_blend() { assertCelebrationBlend(BlendMode::Lighten); }

// Mid-cycle it ends on the very next frame. resetAnimations() also clears V1's bar,
// so the reference gets the same call.
static void test_celebration_stops_on_reset_animations() {
    const BlendMode modes[] = {BlendMode::Normal, BlendMode::Screen, BlendMode::Add, BlendMode::Lighten};
    for (const BlendMode mode : modes) {
        for (int leftWon = 0; leftWon <= 1; leftWon++) {
            Celebration c(mode, leftWon == 1);
            c.renderAt(START + 500);
            c.display.resetAnimations();
            c.plain.resetAnimations();
            c.renderAt(START + 550);
            c.assertEqualsReference(strf("%s leftWon=%d after resetAnimations()", blendName(mode), leftWon).c_str());
        }
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_board_led_count);
    RUN_TEST(test_boot_sweep_lights_only_swept_slots);
    RUN_TEST(test_boot_sweep_never_blanks_a_frame);
    RUN_TEST(test_boot_sweep_ends_dark);
    RUN_TEST(test_boot_sweep_reaches_near_and_both_far_ends);
    RUN_TEST(test_celebration_normal_blend);
    RUN_TEST(test_celebration_screen_blend);
    RUN_TEST(test_celebration_add_blend);
    RUN_TEST(test_celebration_lighten_blend);
    RUN_TEST(test_celebration_stops_on_reset_animations);
    return UNITY_END();
}
