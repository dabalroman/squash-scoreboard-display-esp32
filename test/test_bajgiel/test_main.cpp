// The bajgiel (#61): a game won to zero spins the loser's two 0s as a comet -
// Multiply + LitOnly on layer 2, so it only dims the loser's lit ring, and the
// frame after it expires is the steady score. Both boards, both sides.
#include <cmath>
#include <cstring>   // UserProfile.h relies on Arduino.h for strncpy

#include <unity.h>

#include "DeviceMode/Celebration/CelebrationVariant.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Animation/LedBajgielAnimation.h"
#include "Tournament/Rules/SquashRules.h"
#include "../common/board_config.h"
#include "../common/check.h"
#include "../common/host_globals.h"

static const CRGB OFF(0, 0, 0);
static const CRGB SENTINEL(1, 2, 3);
static const Color LOSER{200, 120, 40};
static const Color WINNER{30, 160, 255};
static const uint32_t START = 9000;
static const uint32_t STEP = 10;

void setUp() {}
void tearDown() {}

static bool isSkip(const int slot) { return LedSlots::POS[slot][0] == LedSlots::SKIP; }

/** A GameCelebration base (11:00 or 00:11, winner blinking) on two displays; only `live` runs the bajgiel. */
struct Celebration {
    bool loserOnLeft;
    CRGB liveBuffer[Board::LED_COUNT];
    CRGB baseBuffer[Board::LED_COUNT];
    LedDisplay live{liveBuffer};
    LedDisplay base{baseBuffer};

    explicit Celebration(const bool loserOnLeft) : loserOnLeft(loserOnLeft) {
        setUpBase(live);
        setUpBase(base);
        g_fakeMillis = START;
        live.startBajgiel(loserOnLeft);
    }

    void setUpBase(LedDisplay &d) const {
        const Color left = loserOnLeft ? LOSER : WINNER;
        const Color right = loserOnLeft ? WINNER : LOSER;
        d.setColonAppearance();
        d.setNumericValue(loserOnLeft ? 0 : 11, loserOnLeft ? 11 : 0);
        d.setGlyphsAppearance(left, right);
        d.setBorderEnabled(true);
        d.setPlayersIndicatorsState(true);
        d.setIndicatorAppearancePlayerA(left, !loserOnLeft);
        d.setIndicatorAppearancePlayerB(right, loserOnLeft);
        d.setBorderAppearance(left, right, !loserOnLeft, loserOnLeft);
        d.setBreathing(0);
    }

    uint16_t loserBits() const { return loserOnLeft ? LedTarget::LeftScore : LedTarget::RightScore; }
    uint16_t digitBit(const int which) const {
        if (loserOnLeft) return which == 0 ? LedTarget::DigitA : LedTarget::DigitB;
        return which == 0 ? LedTarget::DigitC : LedTarget::DigitD;
    }
    bool isLoserSlot(const int i) const { return (live.elementsAt(i) & loserBits()) != 0; }

    void renderAt(const uint32_t elapsed) {
        g_fakeMillis = START + elapsed;
        fillPixels(liveBuffer, Board::LED_COUNT, OFF);
        fillPixels(baseBuffer, Board::LED_COUNT, OFF);
        live.render();
        base.render();
    }
};

/** The standalone animation over the real elementMap, loser on the given side. */
struct Comet {
    uint16_t elementMap[Board::LED_COUNT];
    uint16_t bitA, bitB;
    LedBajgielAnimation anim;
    const LedBajgielAnimation::Params p = LedBajgielAnimation::defaults();

    explicit Comet(const bool loserOnLeft) {
        CRGB scratch[Board::LED_COUNT];
        LedDisplay source(scratch);
        for (int i = 0; i < Board::LED_COUNT; i++) elementMap[i] = source.elementsAt(i);
        bitA = loserOnLeft ? LedTarget::DigitA : LedTarget::DigitC;
        bitB = loserOnLeft ? LedTarget::DigitB : LedTarget::DigitD;
        anim.start(0, elementMap, bitA, bitB);
    }

    /** Slot with the highest level among `bit`'s members, -1 if none is lit. */
    int headOf(const CRGB *out, const uint16_t bit) const {
        int best = -1;
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (!(elementMap[i] & bit) || isSkip(i)) continue;
            if (best < 0 || out[i].r > out[best].r) best = i;
        }
        return best >= 0 && out[best].r > 0 ? best : -1;
    }

    /** Bounding-box centre of a digit, computed here independently of the animation. */
    void centreOf(const uint16_t bit, float &cx, float &cy) const {
        int minX = 32767, maxX = -32767, minY = 32767, maxY = -32767;
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (!(elementMap[i] & bit) || isSkip(i)) continue;
            const int x = LedSlots::POS[i][0], y = LedSlots::POS[i][1];
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
        }
        cx = (minX + maxX) * 0.5f;
        cy = (minY + maxY) * 0.5f;
    }
};

static const bool SIDES[] = {true, false};

// ------------------------------------------------------------- selection ---

static void test_variant_is_bajgiel_only_when_the_loser_has_zero() {
    UserProfile a(0, 1, "A", LOSER);
    UserProfile b(1, 2, "B", WINNER);
    SquashRules rules;
    const Match match(0, a, b, &rules);

    TEST_ASSERT_TRUE(selectCelebrationVariant(match, GameResult{0, 0, 1, 11, 1}) == CelebrationVariant::Bajgiel);
    TEST_ASSERT_TRUE(selectCelebrationVariant(match, GameResult{0, 6, 1, 0, 0}) == CelebrationVariant::Bajgiel);
    TEST_ASSERT_TRUE(selectCelebrationVariant(match, GameResult{0, 1, 1, 11, 1}) == CelebrationVariant::Normal);
    TEST_ASSERT_TRUE(selectCelebrationVariant(match, GameResult{0, 7, 1, 6, 0}) == CelebrationVariant::Normal);
}

// ------------------------------------------------------------- animation ---

static void test_writes_only_the_loser_digits_never_skip() {
    for (const bool side : SIDES) {
        Comet c(side);
        for (uint32_t t = 0; t < c.p.durationMs; t += STEP) {
            CRGB out[Board::LED_COUNT];
            fillPixels(out, Board::LED_COUNT, SENTINEL);
            c.anim.renderFrame(t, out);
            for (int i = 0; i < Board::LED_COUNT; i++) {
                const bool loser = (c.elementMap[i] & (c.bitA | c.bitB)) != 0;
                if (isSkip(i) || !loser) {
                    CHECK(out[i] == SENTINEL, "side %d t=%u wrote slot %d (skip=%d)", side, t, i, isSkip(i));
                } else {
                    CHECK(out[i].r == out[i].g && out[i].g == out[i].b, "side %d t=%u slot %d not grey", side, t, i);
                }
            }
        }
    }
}

// Clockwise as seen on the panel (+y down), never backwards, one turn per period.
static void test_head_advances_clockwise() {
    for (const bool side : SIDES) {
        Comet c(side);
        for (const uint16_t bit : {c.bitA, c.bitB}) {
            float cx, cy;
            c.centreOf(bit, cx, cy);
            int previous = -1;
            float turned = 0.0f;
            for (uint32_t t = 0; t <= c.p.periodMs; t += STEP) {
                CRGB out[Board::LED_COUNT];
                fillPixels(out, Board::LED_COUNT, OFF);
                c.anim.renderFrame(t, out);
                const int head = c.headOf(out, bit);
                CHECK(head >= 0, "side %d t=%u: no lit head in digit bit %u", side, t, bit);
                if (previous >= 0 && head != previous) {
                    const float ax = LedSlots::POS[previous][0] - cx, ay = LedSlots::POS[previous][1] - cy;
                    const float bx = LedSlots::POS[head][0] - cx, by = LedSlots::POS[head][1] - cy;
                    const float cross = ax * by - ay * bx;   // > 0 is clockwise with +y down
                    CHECK(cross > 0.0f, "side %d t=%u: head moved %d -> %d anticlockwise", side, t, previous, head);
                    turned += std::atan2(cross, ax * bx + ay * by);
                }
                previous = head;
            }
            CHECK(std::fabs(turned - 6.2831853f) < 1.6f, "side %d bit %u: head turned %.2f rad in one period", side, bit, turned);
        }
    }
}

// Both 0s spin in sync: their heads sit at the same angle around their own centre.
static void test_glyphs_in_phase() {
    for (const bool side : SIDES) {
        Comet c(side);
        float ax0, ay0, bx0, by0;
        c.centreOf(c.bitA, ax0, ay0);
        c.centreOf(c.bitB, bx0, by0);
        for (uint32_t t = 0; t < c.p.durationMs; t += STEP) {
            CRGB out[Board::LED_COUNT];
            fillPixels(out, Board::LED_COUNT, OFF);
            c.anim.renderFrame(t, out);
            const int ha = c.headOf(out, c.bitA), hb = c.headOf(out, c.bitB);
            const float a = std::atan2(LedSlots::POS[ha][1] - ay0, LedSlots::POS[ha][0] - ax0);
            const float b = std::atan2(LedSlots::POS[hb][1] - by0, LedSlots::POS[hb][0] - bx0);
            float diff = std::fabs(a - b);
            if (diff > 3.14159265f) diff = 6.2831853f - diff;
            CHECK(diff < 0.35f, "side %d t=%u: heads %d/%d are %.2f rad apart", side, t, ha, hb, diff);
        }
    }
}

// -------------------------------------------------------- via LedDisplay ---

// Everything but the loser's digits is the GameCelebration base, byte for byte.
static void test_only_loser_digits_differ_from_base() {
    for (const bool side : SIDES) {
        Celebration f(side);
        for (uint32_t t = 0; t < LedBajgielAnimation::defaults().durationMs; t += STEP) {
            f.renderAt(t);
            for (int i = 0; i < Board::LED_COUNT; i++) {
                if (f.isLoserSlot(i)) continue;
                CHECK_RGB(f.baseBuffer[i], f.liveBuffer[i], "side %d t=%u slot %d", side, t, i);
            }
        }
    }
}

// Multiply only dims, and LitOnly leaves the 0's unlit centre and dead slots dark.
static void test_never_brighter_than_base() {
    for (const bool side : SIDES) {
        Celebration f(side);
        for (uint32_t t = 0; t < LedBajgielAnimation::defaults().durationMs; t += STEP) {
            f.renderAt(t);
            for (int i = 0; i < Board::LED_COUNT; i++) {
                const CRGB l = f.liveBuffer[i], b = f.baseBuffer[i];
                CHECK(l.r <= b.r && l.g <= b.g && l.b <= b.b,
                      "side %d t=%u slot %d %s brighter than base %s", side, t, i, rgb(l).c_str(), rgb(b).c_str());
            }
        }
    }
}

// Every frame, each loser 0 has a bright head and a dark stretch of ring.
static void test_each_zero_has_head_and_dark_ring() {
    for (const bool side : SIDES) {
        Celebration f(side);
        for (uint32_t t = 0; t < LedBajgielAnimation::defaults().durationMs; t += STEP) {
            f.renderAt(t);
            for (int which = 0; which < 2; which++) {
                const uint16_t bit = f.digitBit(which);
                bool head = false, dark = false;
                for (int i = 0; i < Board::LED_COUNT; i++) {
                    if (!(f.live.elementsAt(i) & bit) || f.baseBuffer[i] == OFF) continue;
                    if (f.liveBuffer[i].r * 2 >= f.baseBuffer[i].r) head = true;
                    if (f.liveBuffer[i] == OFF) dark = true;
                }
                CHECK(head && dark, "side %d t=%u digit bit %u: head=%d dark=%d", side, t, bit, head, dark);
            }
        }
    }
}

// Expiry leaves the steady score - the summary's frame, no dark frame between.
static void test_expires_into_the_base() {
    for (const bool side : SIDES) {
        Celebration f(side);
        const uint32_t duration = LedBajgielAnimation::defaults().durationMs;
        g_fakeMillis = START + duration - 1;
        TEST_ASSERT_TRUE_MESSAGE(f.live.celebrationActive(), "inactive before durationMs");
        g_fakeMillis = START + duration;
        TEST_ASSERT_FALSE_MESSAGE(f.live.celebrationActive(), "still active at durationMs");

        f.renderAt(duration);
        for (int i = 0; i < Board::LED_COUNT; i++) CHECK_RGB(f.baseBuffer[i], f.liveBuffer[i], "side %d slot %d", side, i);
    }
}

// C/D -> summary: stopCelebration() ends it on the very next frame.
static void test_stop_celebration_ends_it() {
    for (const bool side : SIDES) {
        Celebration f(side);
        f.renderAt(1234);
        f.live.stopCelebration();
        TEST_ASSERT_FALSE_MESSAGE(f.live.celebrationActive(), "active after stopCelebration()");
        f.renderAt(1244);
        for (int i = 0; i < Board::LED_COUNT; i++) CHECK_RGB(f.baseBuffer[i], f.liveBuffer[i], "side %d slot %d", side, i);
    }
}

// An Overlay mid-bajgiel calls resetAnimations(); the view then hands over, no replay.
static void test_reset_animations_ends_it() {
    Celebration f(true);
    f.renderAt(2000);
    f.live.resetAnimations();
    TEST_ASSERT_FALSE_MESSAGE(f.live.celebrationActive(), "active after resetAnimations()");
}

// The Normal sweep that replaces it on the next game does not inherit it.
static void test_start_celebration_replaces_it() {
    Celebration f(true);
    f.live.startCelebration(WINNER, false);
    g_fakeMillis = START + LedBajgielAnimation::defaults().durationMs - 1;   // the 2.4 s sweep is long over
    TEST_ASSERT_FALSE_MESSAGE(f.live.celebrationActive(), "bajgiel kept celebrationActive() alive under the sweep");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_variant_is_bajgiel_only_when_the_loser_has_zero);
    RUN_TEST(test_writes_only_the_loser_digits_never_skip);
    RUN_TEST(test_head_advances_clockwise);
    RUN_TEST(test_glyphs_in_phase);
    RUN_TEST(test_only_loser_digits_differ_from_base);
    RUN_TEST(test_never_brighter_than_base);
    RUN_TEST(test_each_zero_has_head_and_dark_ring);
    RUN_TEST(test_expires_into_the_base);
    RUN_TEST(test_stop_celebration_ends_it);
    RUN_TEST(test_reset_animations_ends_it);
    RUN_TEST(test_start_celebration_replaces_it);
    return UNITY_END();
}
