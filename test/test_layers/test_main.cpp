// LED layer engine: blend maths, the slot -> element map, LedLayerStack::compose(),
// the breathing animation, bar owner bits and game-ball breathing. Both boards.
#include <array>

#include <unity.h>

#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Animation/LedSlotPositions.h"
#include "Display/LedDisplay/Layers/LedBlend.h"
#include "Display/LedDisplay/Layers/LedBreathingAnimation.h"
#include "Display/LedDisplay/Layers/LedLayerStack.h"
#include "Display/LedDisplay/Layers/LedTarget.h"
#include "Display/LedDisplay/Renderer/GameScoreHistoryBarRenderer.h"
#include "../common/board_config.h"
#include "../common/check.h"
#include "../common/host_globals.h"

typedef std::array<LedBarPixel, LedBar::PIXEL_COUNT> BarPixels;

void setUp() {}
void tearDown() {}

// ------------------------------------------------------------------- blend ---

// Reference formulas in plain int maths, independent of LedBlend's div255 trick.
static int referenceChannel(const BlendMode mode, const int a, const int b, const int alpha) {
    switch (mode) {
        case BlendMode::Screen: return 255 - (255 - a) * (255 - b) / 255;
        case BlendMode::Add: return a + b > 255 ? 255 : a + b;
        case BlendMode::Lighten: return a > b ? a : b;
        case BlendMode::Multiply: return a * b / 255;
        case BlendMode::Normal: {
            const int out = a * (255 - alpha) / 255 + b;
            return out > 255 ? 255 : out;
        }
    }
    return -1;
}

static void test_div255_matches_integer_division() {
    for (int x = 0; x <= 255 * 255; x++) {
        CHECK(LedBlend::div255(static_cast<uint16_t>(x)) == x / 255, "div255(%d)", x);
    }
}

// Exhaustive over grey pairs: every channel sees every (a, b), and alpha = b.
static void assertBlendExhaustive(const BlendMode mode) {
    for (int a = 0; a < 256; a++) {
        for (int b = 0; b < 256; b++) {
            const CRGB out = blendPixel(CRGB(a, a, a), CRGB(b, b, b), mode);
            const int expected = referenceChannel(mode, a, b, b);
            CHECK_RGB(CRGB(expected, expected, expected), out, "%s a=%d b=%d", blendName(mode), a, b);

            const int o = out.r;
            switch (mode) {
                case BlendMode::Lighten:
                case BlendMode::Screen: CHECK(o >= a && o >= b, "%s a=%d b=%d -> %d darker than an input", blendName(mode), a, b, o); break;
                case BlendMode::Add: CHECK(o >= a, "Add a=%d b=%d -> %d darker than the base", a, b, o); break;
                case BlendMode::Multiply: CHECK(o <= a && o <= b, "Multiply a=%d b=%d -> %d brighter than an input", a, b, o); break;
                case BlendMode::Normal: CHECK(o >= b, "Normal a=%d b=%d -> %d darker than the layer", a, b, o); break;
            }
        }
    }
}

static void test_blend_normal_exhaustive() { assertBlendExhaustive(BlendMode::Normal); }
static void test_blend_screen_exhaustive() { assertBlendExhaustive(BlendMode::Screen); }
static void test_blend_add_exhaustive() { assertBlendExhaustive(BlendMode::Add); }
static void test_blend_lighten_exhaustive() { assertBlendExhaustive(BlendMode::Lighten); }
static void test_blend_multiply_exhaustive() { assertBlendExhaustive(BlendMode::Multiply); }

// On coloured bases, where Normal's alpha is not the channel itself.
static void test_blend_identity_and_normal_opacity() {
    const BlendMode modes[] = {BlendMode::Normal, BlendMode::Screen, BlendMode::Add, BlendMode::Lighten, BlendMode::Multiply};
    const uint8_t levels[] = {0, 1, 17, 64, 127, 128, 200, 254, 255};
    for (const BlendMode mode : modes) {
        for (const uint8_t r : levels) {
            for (const uint8_t g : levels) {
                for (const uint8_t b : levels) {
                    const CRGB base(r, g, b);
                    CHECK_RGB(base, blendPixel(base, identityFor(mode), mode), "%s identity on %s", blendName(mode), rgb(base).c_str());
                    // A fully bright channel makes Normal opaque: the layer replaces the base.
                    const CRGB opaque(255, g, b);
                    if (mode == BlendMode::Normal) {
                        CHECK_RGB(opaque, blendPixel(base, opaque, mode), "Normal alpha 255 over %s", rgb(base).c_str());
                    }
                }
            }
        }
    }
}

// ------------------------------------------------------------- element map ---

static const uint16_t OWNER_BITS = LedTarget::BarLeft | LedTarget::BarRight;

// a, b, a committed, then an uncommitted b: both owners and a blinking pixel.
static GameScoreHistory ownerCheckHistory() {
    GameScoreHistory history;
    history.scorePoint(GameSide::a);
    history.scorePoint(GameSide::b);
    history.scorePoint(GameSide::a);
    history.commit();
    history.scorePoint(GameSide::b);
    return history;
}

static BarPixels ownerCheckPixels(const GameScoreHistory &history) {
    return GameScoreHistoryBarRenderer::toLedBarPixels(Colors::Red, Colors::Blue, history, 1, 2);
}

// Owners marked, so the one-element checks must see past them.
struct MappedDisplay {
    CRGB buffer[Board::LED_COUNT];
    LedDisplay display{buffer};
    GameScoreHistory history = ownerCheckHistory();

    MappedDisplay() { display.setLedBarState([&] { return ownerCheckPixels(history); }); }
};

// Each glyph position's bit covers exactly its segment table's slots.
static void test_element_map_matches_segment_tables() {
    MappedDisplay m;
    const struct { GlyphId id; uint16_t bit; } glyphs[] = {
        {GlyphId::A, LedTarget::DigitA}, {GlyphId::B, LedTarget::DigitB},
        {GlyphId::C, LedTarget::DigitC}, {GlyphId::D, LedTarget::DigitD},
        {GlyphId::Colon, LedTarget::Colon},
        {GlyphId::IndicatorPlayerA, LedTarget::IndicatorA},
        {GlyphId::IndicatorPlayerB, LedTarget::IndicatorB},
    };
    for (const auto &glyph : glyphs) {
        bool inTable[Board::LED_COUNT] = {};
        const SegmentTable table = ActiveGlyphProfile::segmentsFor(glyph.id);
        for (uint8_t s = 0; s < table.count; s++) {
            for (uint8_t i = 0; i < table.segments[s].count; i++) {
                inTable[table.base + table.segments[s].pixels[i]] = true;
            }
        }
        for (int slot = 0; slot < Board::LED_COUNT; slot++) {
            const bool marked = (m.display.elementsAt(slot) & glyph.bit) != 0;
            CHECK(marked == inTable[slot], "bit 0x%x slot %d: marked=%d, in segment table=%d", glyph.bit, slot, marked, inTable[slot]);
        }
    }
}

static void test_element_map_one_element_per_slot() {
    MappedDisplay m;
    for (int slot = 0; slot < Board::LED_COUNT; slot++) {
        // Owner bits ride on Bar slots on top of the element bit.
        const uint16_t bits = m.display.elementsAt(slot) & static_cast<uint16_t>(~OWNER_BITS);
        CHECK(!(bits & (bits - 1)), "slot %d in two elements (0x%x)", slot, bits);
    }
}

static void test_element_map_front_is_exactly_the_live_slots() {
    MappedDisplay m;
    for (int slot = 0; slot < Board::LED_COUNT; slot++) {
        const bool front = (m.display.elementsAt(slot) & LedTarget::Front) != 0;
        const bool live = LedSlots::POS[slot][0] != LedSlots::SKIP;
        CHECK(front == live, "slot %d: front=%d but live=%d", slot, front, live);
    }
}

static void test_element_map_indicators_and_counts() {
    MappedDisplay m;
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(LedTarget::IndicatorA, m.display.elementsAt(BOARD.indicatorA), "indicator A slot");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(LedTarget::IndicatorB, m.display.elementsAt(BOARD.indicatorB), "indicator B slot");

    int colon = 0, bar = 0, top = 0, bottom = 0;
    for (int slot = 0; slot < Board::LED_COUNT; slot++) {
        const uint16_t bits = m.display.elementsAt(slot);
        if (bits & LedTarget::Colon) colon++;
        if (bits & LedTarget::Bar) bar++;
        if (bits & LedTarget::BorderTop) top++;
        if (bits & LedTarget::BorderBottom) bottom++;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(BOARD.colonSlots, colon, "colon slots");
    TEST_ASSERT_EQUAL_INT_MESSAGE(BOARD.barSlots, bar, "bar slots");
    TEST_ASSERT_EQUAL_INT_MESSAGE(BOARD.borderTopSlots, top, "border top slots");
    TEST_ASSERT_EQUAL_INT_MESSAGE(BOARD.borderBottomSlots, bottom, "border bottom slots");
}

// ----------------------------------------------------------------- compose ---

// Fills every slot with one colour; active only while `on`.
class FillAnimation : public LedAnimation {
public:
    CRGB color;
    bool on;
    bool writes;

    FillAnimation(const CRGB color, const bool on, const bool writes = true) : color(color), on(on), writes(writes) {}

    bool active(uint32_t) const override { return on; }

    void render(uint32_t, CRGB *out) const override {
        if (!writes) return;
        for (int i = 0; i < Board::LED_COUNT; i++) out[i] = color;
    }
};

// Even slots digit A, odd slots digit B; every third slot lit.
struct ComposeFixture {
    uint16_t map[Board::LED_COUNT];
    CRGB base[Board::LED_COUNT];
    CRGB pixels[Board::LED_COUNT];
    LedLayerStack stack{map};

    ComposeFixture() {
        for (int i = 0; i < Board::LED_COUNT; i++) {
            map[i] = i % 2 ? LedTarget::DigitB : LedTarget::DigitA;
            base[i] = i % 3 == 0 ? CRGB(40, 80, 120) : CRGB(0, 0, 0);
            pixels[i] = base[i];
        }
    }

    void assertUnchanged() const {
        for (int i = 0; i < Board::LED_COUNT; i++) CHECK_RGB(base[i], pixels[i], "slot %d", i);
    }
};

static void test_compose_empty_stack_changes_nothing() {
    ComposeFixture f;
    f.stack.compose(f.pixels, 0);
    f.assertUnchanged();
}

static void test_compose_inactive_layer_changes_nothing() {
    ComposeFixture f;
    FillAnimation white(CRGB(255, 255, 255), false);
    f.stack.set(LedLayerStack::LAYER_2, &white, BlendMode::Add, LedTarget::All, LayerMask::AllSlots);
    f.stack.compose(f.pixels, 0);
    f.assertUnchanged();
}

// Multiply with nothing written: the white identity fill must leave the base alone.
static void test_compose_multiply_identity_fill_changes_nothing() {
    ComposeFixture f;
    FillAnimation silent(CRGB(0, 0, 0), true, false);
    f.stack.set(LedLayerStack::LAYER_1, &silent, BlendMode::Multiply, LedTarget::All, LayerMask::AllSlots);
    f.stack.compose(f.pixels, 0);
    f.assertUnchanged();
}

static void test_compose_target_limits_slots() {
    ComposeFixture f;
    FillAnimation white(CRGB(255, 255, 255), true);
    f.stack.set(LedLayerStack::LAYER_2, &white, BlendMode::Add, LedTarget::DigitA, LayerMask::AllSlots);
    f.stack.compose(f.pixels, 0);
    for (int i = 0; i < Board::LED_COUNT; i++) {
        CHECK_RGB(i % 2 ? f.base[i] : CRGB(255, 255, 255), f.pixels[i], "slot %d (only digit A may change)", i);
    }
}

static void test_compose_lit_only_skips_dark_slots() {
    ComposeFixture f;
    FillAnimation white(CRGB(255, 255, 255), true);
    f.stack.set(LedLayerStack::LAYER_1, &white, BlendMode::Add, LedTarget::All, LayerMask::LitOnly);
    f.stack.compose(f.pixels, 0);
    for (int i = 0; i < Board::LED_COUNT; i++) {
        CHECK_RGB(i % 3 == 0 ? CRGB(255, 255, 255) : CRGB(0, 0, 0), f.pixels[i], "slot %d", i);
    }
}

// Layer 1 lights everything; layer 2 (LitOnly) sees the base's lit set, not layer 1's output.
static void test_compose_order_and_lit_capture() {
    ComposeFixture f;
    FillAnimation red(CRGB(100, 0, 0), true);
    FillAnimation half(CRGB(128, 128, 128), true);
    f.stack.set(LedLayerStack::LAYER_2, &half, BlendMode::Multiply, LedTarget::All, LayerMask::LitOnly);
    f.stack.set(LedLayerStack::LAYER_1, &red, BlendMode::Add, LedTarget::All, LayerMask::AllSlots);
    f.stack.compose(f.pixels, 0);
    for (int i = 0; i < Board::LED_COUNT; i++) {
        const CRGB added = blendPixel(f.base[i], CRGB(100, 0, 0), BlendMode::Add);
        const CRGB expected = i % 3 == 0 ? blendPixel(added, CRGB(128, 128, 128), BlendMode::Multiply) : added;
        CHECK_RGB(expected, f.pixels[i], "slot %d", i);
    }
}

// --------------------------------------------------------------- breathing ---

static void test_breathing_spans_floor_to_full() {
    const uint8_t minLevel = 102;
    LedBreathingAnimation breathing(2000, minLevel);
    breathing.start(1000);

    int lowest = 255, highest = 0;
    for (uint32_t t = 1000; t < 1000 + 4000; t++) {
        const int level = breathing.levelAt(t);
        CHECK(level >= minLevel, "t=%u level %d below the floor %d", t, level, minLevel);
        if (level < lowest) lowest = level;
        if (level > highest) highest = level;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(minLevel, lowest, "lowest level");
    TEST_ASSERT_EQUAL_INT_MESSAGE(255, highest, "highest level");
}

static void test_breathing_renders_uniform_grey() {
    LedBreathingAnimation breathing(2000, 102);
    breathing.start(1000);
    CRGB out[Board::LED_COUNT];
    breathing.render(1500, out);
    const uint8_t level = breathing.levelAt(1500);
    for (int i = 0; i < Board::LED_COUNT; i++) CHECK_RGB(CRGB(level, level, level), out[i], "slot %d", i);
}

static void test_breathing_inactive_after_stop() {
    LedBreathingAnimation breathing(2000, 102);
    breathing.start(1000);
    breathing.stop();
    TEST_ASSERT_FALSE_MESSAGE(breathing.active(1500), "active after stop()");
}

// -------------------------------------------------------------- bar owners ---

// Owner bits must equal the pixels' sides on Bar slots (in slot order) and sit nowhere else.
static void assertOwners(const char *what, const LedDisplay &display, const BarPixels *pixels) {
    int barIndex = 0;
    for (int slot = 0; slot < Board::LED_COUNT; slot++) {
        const uint16_t bits = display.elementsAt(slot);
        uint16_t expected = 0;
        if (bits & LedTarget::Bar) {
            const GameSide side = pixels ? (*pixels)[barIndex].side : GameSide::none;
            expected = side == GameSide::a ? LedTarget::BarLeft : side == GameSide::b ? LedTarget::BarRight : 0;
            barIndex++;
        }
        CHECK((bits & OWNER_BITS) == expected, "%s: slot %d bits 0x%x, expected owner 0x%x", what, slot, bits, expected);
    }
}

// Point width 2, padding 1: four points -> 4 left and 4 right pixels.
static void test_bar_renderer_marks_sides() {
    const BarPixels pixels = ownerCheckPixels(ownerCheckHistory());
    int left = 0, right = 0;
    for (const LedBarPixel &p : pixels) {
        left += p.side == GameSide::a;
        right += p.side == GameSide::b;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, left, "left-owned pixels");
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, right, "right-owned pixels");

    // Every other bar renderer leaves the owner at none.
    const LedBarPixel plain;
    TEST_ASSERT_TRUE_MESSAGE(plain.side == GameSide::none, "LedBarPixel default side not none");
}

// V2 has no bar: its setter runs nothing, so no slot may ever carry an owner.
static void test_bar_owners_follow_every_state() {
    CRGB buffer[Board::LED_COUNT];
    LedDisplay display(buffer);
    const bool hasBar = BOARD.barSlots > 0;

    assertOwners("fresh", display, nullptr);

    const GameScoreHistory mixed = ownerCheckHistory();
    const BarPixels mixedPixels = ownerCheckPixels(mixed);
    display.setLedBarState([&] { return ownerCheckPixels(mixed); });
    assertOwners("mixed", display, hasBar ? &mixedPixels : nullptr);

    // A new state re-marks every slot: nothing left over from the mixed one.
    GameScoreHistory onlyB;
    onlyB.scorePoint(GameSide::b);
    onlyB.commit();
    const BarPixels onlyBPixels = ownerCheckPixels(onlyB);
    display.setLedBarState([&] { return ownerCheckPixels(onlyB); });
    assertOwners("new state", display, hasBar ? &onlyBPixels : nullptr);

    display.setLedBarState([&] { return ownerCheckPixels(mixed); });
    display.resetAnimations();
    assertOwners("after resetAnimations", display, nullptr);
}

// --------------------------------------------------------------- game ball ---

static void test_breathing_targets_per_side() {
    const uint16_t left = LedTarget::LeftScore | LedTarget::BorderTop | LedTarget::BarLeft;
    const uint16_t right = LedTarget::RightScore | LedTarget::BorderBottom | LedTarget::BarRight;
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, LedDisplay::breathingTargets(false, false), "nobody");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(left, LedDisplay::breathingTargets(true, false), "left");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(right, LedDisplay::breathingTargets(false, true), "right");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(left | right, LedDisplay::breathingTargets(true, true), "both");
}

// A lit game screen: 88-88, colon, indicators, border and (V1) the mixed history bar.
static void gameBallScreen(LedDisplay &display, const GameScoreHistory &history, const bool blinkLeft) {
    display.setNumericValue(88, 88);
    display.setGlyphsAppearance(Colors::Orange, Colors::Aqua, blinkLeft, false);
    display.setColonAppearance(Colors::White);
    display.setPlayersIndicatorsState(true);
    display.setIndicatorAppearancePlayerA(Colors::Orange);
    display.setIndicatorAppearancePlayerB(Colors::Aqua);
    display.setBorderEnabled(true);
    display.setBorderAppearance(Colors::Orange, Colors::Aqua);
    display.setLedBarState([&] { return ownerCheckPixels(history); });
}

// render() skips blink-dark slots rather than clearing them, so start from black.
static void renderAt(LedDisplay &display, CRGB *buffer, const uint32_t nowMs, CRGB *out) {
    g_fakeMillis = nowMs;
    fillPixels(buffer, Board::LED_COUNT, CRGB(0, 0, 0));
    display.render();
    for (int i = 0; i < Board::LED_COUNT; i++) out[i] = buffer[i];
}

// Which slots a side's breathing may reach - bar ownership from the renderer's own
// pixels, not the element map, so a mis-marked owner cannot hide itself.
static bool inSide(const uint16_t bits, const int barIndex, const bool left, const BarPixels &bar) {
    if (bits & (left ? (LedTarget::LeftScore | LedTarget::BorderTop) : (LedTarget::RightScore | LedTarget::BorderBottom))) {
        return true;
    }
    return barIndex >= 0 && bar[barIndex].side == (left ? GameSide::a : GameSide::b);
}

static void assertGameBallBreathesOneSide(const bool left) {
    const GameScoreHistory history = ownerCheckHistory();
    const BarPixels bar = ownerCheckPixels(history);
    const uint32_t started = 1000;
    CRGB buffer[Board::LED_COUNT];
    CRGB breathed[Board::LED_COUNT];
    CRGB base[Board::LED_COUNT];
    LedDisplay display(buffer);
    gameBallScreen(display, history, false);

    const uint16_t mine = LedDisplay::breathingTargets(left, !left);
    const uint16_t both = LedDisplay::breathingTargets(true, true);

    // Repeat calls, including one that moves the mask, keep the phase: 700 ms in is
    // the trough (102), where a restart would still read 255.
    g_fakeMillis = started;
    display.setBreathing(both);
    g_fakeMillis = started + 350;
    display.setBreathing(both);
    display.setBreathing(mine);
    renderAt(display, buffer, started + 700, breathed);
    display.setBreathing(0);
    renderAt(display, buffer, started + 700, base);

    LedBreathingAnimation phase;   // the production period and floor
    phase.start(started);
    const uint8_t level = phase.levelAt(started + 700);
    TEST_ASSERT_EQUAL_INT_MESSAGE(102, level, "trough level 700 ms in");

    int touched = 0, barTouched = 0, borderTouched = 0;
    int barIndex = 0;
    for (int slot = 0; slot < Board::LED_COUNT; slot++) {
        const uint16_t bits = display.elementsAt(slot);
        const int bi = (bits & LedTarget::Bar) ? barIndex++ : -1;
        const bool lit = base[slot].r | base[slot].g | base[slot].b;
        const bool dims = lit && inSide(bits, bi, left, bar);
        const CRGB expected = dims ? blendPixel(base[slot], CRGB(level, level, level), BlendMode::Multiply) : base[slot];
        CHECK_RGB(expected, breathed[slot], "slot %d bits 0x%x", slot, bits);
        if (dims) {
            touched++;
            if (bi >= 0) barTouched++;
            if (bits & (LedTarget::BorderTop | LedTarget::BorderBottom)) borderTouched++;
        }
    }
    // Only meaningful if every part of the side was lit and so actually dimmed.
    TEST_ASSERT_TRUE_MESSAGE(touched > 0, "nothing dimmed");
    if (BOARD.barSlots) TEST_ASSERT_TRUE_MESSAGE(barTouched > 0, "no bar pixel dimmed");
    if (BOARD.borderTopSlots) TEST_ASSERT_TRUE_MESSAGE(borderTouched > 0, "no border slot dimmed");

    // Off -> on restarts at 255, so switching it on never jumps dark.
    g_fakeMillis = started + 700;
    display.setBreathing(mine);
    renderAt(display, buffer, started + 700, breathed);
    for (int slot = 0; slot < Board::LED_COUNT; slot++) {
        CHECK_RGB(base[slot], breathed[slot], "restart not at full level, slot %d", slot);
    }
}

static void test_game_ball_breathes_left_side_only() { assertGameBallBreathesOneSide(true); }
static void test_game_ball_breathes_right_side_only() { assertGameBallBreathesOneSide(false); }

// Blink + breathing on the left: the dark phase stays black, the lit phase is the
// breathing-scaled colour, the right side never moves.
static void test_game_ball_breathing_over_blink() {
    const GameScoreHistory history = ownerCheckHistory();
    const BarPixels bar = ownerCheckPixels(history);
    const uint32_t started = 1000;
    CRGB buffer[Board::LED_COUNT];
    CRGB breathed[Board::LED_COUNT];
    CRGB base[Board::LED_COUNT];
    LedDisplay display(buffer);
    gameBallScreen(display, history, true);

    const uint32_t times[] = {2000, 2300};   // % 500: 0 = dark phase, 300 = lit
    for (const uint32_t t : times) {
        g_fakeMillis = started;
        display.setBreathing(LedDisplay::breathingTargets(true, false));
        renderAt(display, buffer, t, breathed);
        display.setBreathing(0);
        renderAt(display, buffer, t, base);

        LedBreathingAnimation phase;
        phase.start(started);
        const uint8_t level = phase.levelAt(t);
        const bool dark = t % 500 < 250;

        int barIndex = 0, leftLit = 0;
        for (int slot = 0; slot < Board::LED_COUNT; slot++) {
            const uint16_t bits = display.elementsAt(slot);
            const int bi = (bits & LedTarget::Bar) ? barIndex++ : -1;
            const bool leftDigit = (bits & LedTarget::LeftScore) != 0;
            const bool lit = base[slot].r | base[slot].g | base[slot].b;

            CRGB expected = base[slot];
            if (leftDigit && dark) {
                expected = CRGB(0, 0, 0);
            } else if (lit && inSide(bits, bi, true, bar)) {
                expected = blendPixel(base[slot], CRGB(level, level, level), BlendMode::Multiply);
            }
            CHECK_RGB(expected, breathed[slot], "t=%u slot %d", t, slot);
            if (leftDigit && lit) leftLit++;
        }
        // Otherwise the dark-phase case would hold trivially.
        CHECK(dark == (leftLit == 0), "t=%u: %d left digit slots lit in the %s phase", t, leftLit, dark ? "dark" : "lit");
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_div255_matches_integer_division);
    RUN_TEST(test_blend_normal_exhaustive);
    RUN_TEST(test_blend_screen_exhaustive);
    RUN_TEST(test_blend_add_exhaustive);
    RUN_TEST(test_blend_lighten_exhaustive);
    RUN_TEST(test_blend_multiply_exhaustive);
    RUN_TEST(test_blend_identity_and_normal_opacity);
    RUN_TEST(test_element_map_matches_segment_tables);
    RUN_TEST(test_element_map_one_element_per_slot);
    RUN_TEST(test_element_map_front_is_exactly_the_live_slots);
    RUN_TEST(test_element_map_indicators_and_counts);
    RUN_TEST(test_compose_empty_stack_changes_nothing);
    RUN_TEST(test_compose_inactive_layer_changes_nothing);
    RUN_TEST(test_compose_multiply_identity_fill_changes_nothing);
    RUN_TEST(test_compose_target_limits_slots);
    RUN_TEST(test_compose_lit_only_skips_dark_slots);
    RUN_TEST(test_compose_order_and_lit_capture);
    RUN_TEST(test_breathing_spans_floor_to_full);
    RUN_TEST(test_breathing_renders_uniform_grey);
    RUN_TEST(test_breathing_inactive_after_stop);
    RUN_TEST(test_bar_renderer_marks_sides);
    RUN_TEST(test_bar_owners_follow_every_state);
    RUN_TEST(test_breathing_targets_per_side);
    RUN_TEST(test_game_ball_breathes_left_side_only);
    RUN_TEST(test_game_ball_breathes_right_side_only);
    RUN_TEST(test_game_ball_breathing_over_blink);
    return UNITY_END();
}
