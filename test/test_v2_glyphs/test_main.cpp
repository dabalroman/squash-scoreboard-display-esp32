// V2 only: nine-segment glyph writes stay inside their module and off the dead
// slots, border and back indicators are independent, and the per-segment brightness
// compensation matches the measured areas without raising the power bound.
#include <cmath>

#include <unity.h>

#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Profiles/NineSegmentBrightness.h"
#include "../common/check.h"
#include "../common/host_globals.h"

static const int GUARD = 256;   // oversized so out-of-range writes are caught
static const CRGB SENTINEL(1, 2, 3);
static const CRGB OFF(0, 0, 0);

void setUp() {}
void tearDown() {}

// ------------------------------------------------------------------- glyphs ---

static bool allowed(const int id, const int slot) {
    if (slot >= 74 || slot == 12 || slot == 28 || slot == 44 || slot == 60) return false;
    switch (static_cast<GlyphId>(id)) {
        case GlyphId::A: return slot >= 58 && slot <= 73;
        case GlyphId::B: return slot >= 42 && slot <= 57;
        case GlyphId::C: return slot >= 26 && slot <= 41;
        case GlyphId::D: return slot >= 10 && slot <= 25;
        case GlyphId::IndicatorPlayerA: return slot == 9;
        case GlyphId::IndicatorPlayerB: return slot == 4;
        case GlyphId::Colon: return false;
    }
    return false;
}

// Inside the chain, never a dead slot, digits inside their own module, indicator A
// only 9, B only 4, the colon nothing.
static void test_glyph_writes_stay_in_their_module() {
    for (int g = 0; g < GLYPH_COUNT; g++) {
        for (int id = 0; id < 7; id++) {
            CRGB buffer[GUARD];
            fillPixels(buffer, GUARD, SENTINEL);
            LedGlyph glyph(buffer, static_cast<GlyphId>(id));
            glyph.setGlyph(static_cast<Glyph>(g));
            glyph.setColor(CRGB(200, 100, 50));
            glyph.render(300);

            for (int i = 0; i < GUARD; i++) {
                CHECK(buffer[i] == SENTINEL || allowed(id, i), "glyph %d on position %d wrote slot %d", g, id, i);
            }
        }
    }
}

static void test_digit_8_lights_every_live_slot_of_its_module() {
    CRGB buffer[GUARD];
    fillPixels(buffer, GUARD, SENTINEL);
    LedGlyph glyph(buffer, GlyphId::A);
    glyph.setGlyph(Glyph::D8);
    glyph.render(300);
    int lit = 0;
    for (int i = 58; i <= 73; i++) lit += !(buffer[i] == SENTINEL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(15, lit, "module A slots 58-73 minus dead 60");
}

// ------------------------------------------------------ border / indicators ---

// Border top {2,3,7,8} takes the top colour and bottom {0,1,5,6} the bottom one
// whatever sameSideMode says; indicators (A=9, B=4) swap with it.
static void assertBorderAndIndicatorsIndependent(const bool sameSide) {
    CRGB buffer[GUARD];
    const CRGB a(10, 0, 0);
    const CRGB b(0, 20, 0);
    const CRGB other(0, 0, 30);
    const int top[] = {2, 3, 7, 8};
    const int bottom[] = {0, 1, 5, 6};
    const CRGB expect9 = sameSide ? b : a;
    const CRGB expect4 = sameSide ? a : b;

    LedDisplay display(buffer);
    auto renderAt = [&](const uint32_t ms) {
        fillPixels(buffer, GUARD, OFF);
        g_fakeMillis = ms;
        display.render();
    };
    display.setSameSideMode(sameSide);
    display.setGlyphsGlyph(Glyph::Empty, Glyph::Empty, Glyph::Empty, Glyph::Empty);

    renderAt(1300);   // visible blink phase
    for (int i = 0; i < 10; i++) CHECK_RGB(OFF, buffer[i], "fresh display, slot %d", i);

    display.setPlayersIndicatorsState(true);
    display.setIndicatorAppearancePlayerA(Color(10, 0, 0));
    display.setIndicatorAppearancePlayerB(Color(0, 20, 0));
    display.setBorderEnabled(true);
    display.setBorderAppearance(Color(10, 0, 0), Color(0, 20, 0));
    renderAt(1300);
    for (int k = 0; k < 4; k++) {
        CHECK_RGB(a, buffer[top[k]], "both on, top slot %d", top[k]);
        CHECK_RGB(b, buffer[bottom[k]], "both on, bottom slot %d", bottom[k]);
    }
    CHECK_RGB(expect9, buffer[9], "both on, indicator slot 9");
    CHECK_RGB(expect4, buffer[4], "both on, indicator slot 4");
    for (int i = 10; i < GUARD; i++) CHECK_RGB(OFF, buffer[i], "stray write, slot %d", i);

    // Indicator calls never repaint the border.
    display.setIndicatorAppearancePlayerA(Color(0, 0, 30));
    display.setIndicatorAppearancePlayerB(Color(0, 0, 30));
    renderAt(1300);
    CHECK_RGB(a, buffer[2], "border top after an indicator recolour");
    CHECK_RGB(b, buffer[0], "border bottom after an indicator recolour");
    CHECK_RGB(other, buffer[9], "indicator slot 9 recoloured");
    CHECK_RGB(other, buffer[4], "indicator slot 4 recoloured");

    // Dark blink phase: top half dark, bottom still lit.
    display.setBorderAppearance(Color(10, 0, 0), Color(0, 20, 0), true, false);
    renderAt(1100);
    CHECK_RGB(OFF, buffer[2], "blinking top border, dark phase");
    CHECK_RGB(b, buffer[0], "steady bottom border, dark phase");

    display.setBorderEnabled(false);
    renderAt(1300);
    for (int k = 0; k < 4; k++) {
        CHECK_RGB(OFF, buffer[top[k]], "border off, top slot %d", top[k]);
        CHECK_RGB(OFF, buffer[bottom[k]], "border off, bottom slot %d", bottom[k]);
    }
    CHECK_RGB(other, buffer[9], "border off, indicator slot 9");
    CHECK_RGB(other, buffer[4], "border off, indicator slot 4");

    display.setPlayersIndicatorsState(false);
    display.setBorderEnabled(true);
    renderAt(1300);
    CHECK_RGB(OFF, buffer[9], "indicators off, slot 9");
    CHECK_RGB(OFF, buffer[4], "indicators off, slot 4");
    CHECK_RGB(a, buffer[2], "indicators off, border top");
    CHECK_RGB(b, buffer[0], "indicators off, border bottom");

    display.setBorderEnabled(false);
    renderAt(1300);
    for (int i = 0; i < 10; i++) CHECK_RGB(OFF, buffer[i], "both off, slot %d", i);
}

static void test_border_and_indicators_independent() { assertBorderAndIndicatorsIndependent(false); }
static void test_border_and_indicators_independent_same_side() { assertBorderAndIndicatorsIndependent(true); }

// ------------------------------------------------------------- compensation ---

// Recomputed here from the measured areas, independently of NineSegmentBrightness.
struct SegmentFact {
    const char *name;
    int moduleSlots[3];
    int slotCount;
    int dies;
    float totalMm2;
};

static const SegmentFact SEGMENTS[] = {
    {"bottom",       {11, 12, 13}, 3, 3, 1103.0f},
    {"bottom-left",  {10, 0, 0},   1, 2, 374.0f},
    {"bottom-right", {0, 1, 0},    2, 2, 766.0f},
    {"center",       {15, 0, 0},   1, 2, 349.0f},
    {"upper-left",   {8, 0, 0},    1, 2, 345.0f},
    {"top-right",    {3, 4, 0},    2, 2, 693.0f},
    {"top",          {5, 6, 7},    3, 3, 1044.0f},
    {"mid-left",     {9, 0, 0},    1, 2, 331.0f},
    {"mid-right",    {14, 0, 0},   1, 2, 331.0f},
};
static const float TOLERANCE = 1.2f;
static const float REFERENCE = 383.0f;   // bottom-right's mm^2/die - the largest measured
static const int BORDER_SLOTS[] = {0, 1, 2, 3, 5, 6, 7, 8};

static int expectedFactor(const float mm2PerDie) {
    const float ratio = TOLERANCE * mm2PerDie / REFERENCE;
    return ratio >= 1.0f ? 255 : static_cast<int>(std::lround(ratio * 255.0f));
}

static int expectedBorderFactor() {
    return expectedFactor(473.0f / 2.0f);   // 473 mm^2 across 2 dies per segment, no special case
}

static void test_compensation_segment_factors_match_areas() {
    for (const SegmentFact &seg : SEGMENTS) {
        const int expected = expectedFactor(seg.totalMm2 / static_cast<float>(seg.dies));
        for (int i = 0; i < seg.slotCount; i++) {
            const auto moduleSlot = static_cast<uint16_t>(seg.moduleSlots[i]);
            TEST_ASSERT_EQUAL_INT_MESSAGE(expected, NineSegmentBrightness::moduleFactor(moduleSlot),
                                          strf("segment %s, module slot %d", seg.name, moduleSlot).c_str());
        }
    }
}

static void test_compensation_border_factor_matches_area() {
    for (const int slot : BORDER_SLOTS) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(expectedBorderFactor(), NineSegmentBrightness::slotScale(static_cast<uint16_t>(slot)),
                                      strf("border slot %d", slot).c_str());
    }
}

// Back indicators and every module's dead chain position stay at identity.
static void test_compensation_leaves_indicators_and_dead_slots() {
    const int untouched[] = {4, 9, 12, 28, 44, 60};
    for (const int slot : untouched) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(255, NineSegmentBrightness::slotScale(static_cast<uint16_t>(slot)),
                                      strf("slot %d", slot).c_str());
    }
}

// Four "8"s + border + indicators must stay under the old flat-0.8 bound (0.8 x 90
// dies = 72): compensation replaced that power guard on V2.
static void test_compensation_worst_case_within_power_bound() {
    double perModule = 0.0;
    for (const SegmentFact &seg : SEGMENTS) {
        perModule += seg.dies * (expectedFactor(seg.totalMm2 / static_cast<float>(seg.dies)) / 255.0);
    }
    const double border = 8 * (expectedBorderFactor() / 255.0);   // 8 border slots, 1 die each
    const double indicators = 2 * 1.0;                            // always full
    const double worstCase = 4 * perModule + border + indicators;
    CHECK(worstCase <= 72.0, "worst case %.2f die-equivalents exceeds the 72 bound", worstCase);
}

// Every slot of the chain, not just the sampled ones above.
static void test_compensate_scales_every_slot_by_its_factor() {
    CRGB buffer[Board::LED_COUNT];
    fillPixels(buffer, Board::LED_COUNT, CRGB(255, 255, 255));
    NineSegmentBrightness::compensate(buffer);
    for (int i = 0; i < Board::LED_COUNT; i++) {
        const uint8_t factor = NineSegmentBrightness::slotScale(static_cast<uint16_t>(i));
        CHECK_RGB(CRGB(255, 255, 255).scale8(factor), buffer[i], "slot %d (factor %d)", i, factor);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_glyph_writes_stay_in_their_module);
    RUN_TEST(test_digit_8_lights_every_live_slot_of_its_module);
    RUN_TEST(test_border_and_indicators_independent);
    RUN_TEST(test_border_and_indicators_independent_same_side);
    RUN_TEST(test_compensation_segment_factors_match_areas);
    RUN_TEST(test_compensation_border_factor_matches_area);
    RUN_TEST(test_compensation_leaves_indicators_and_dead_slots);
    RUN_TEST(test_compensation_worst_case_within_power_bound);
    RUN_TEST(test_compensate_scales_every_slot_by_its_factor);
    return UNITY_END();
}
