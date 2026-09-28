// LED layer engine: blend maths, the slot -> element map, LedLayerStack::compose(),
// the breathing animation, bar owner bits and game-ball breathing. Both boards.
#include <array>

#include <unity.h>

#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Animation/LedSlotPositions.h"
#include "Display/LedDisplay/Animation/LedSweepAnimation.h"
#include "Display/LedDisplay/Layers/LedBlend.h"
#include "Display/LedDisplay/Layers/LedBreathingAnimation.h"
#include "Display/LedDisplay/Layers/LedSmokeAnimation.h"
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

// ------------------------------------------------------------ smoke (#59) ---

static bool isSkip(const int slot) {
    return LedSlots::POS[slot][0] == LedSlots::SKIP;
}

static void test_smoke_is_neutral_grey_and_skips_dead_slots() {
    LedSmokeAnimation smoke;
    smoke.start(1000);
    int lit = 0;
    for (uint32_t t = 1000; t < 1000 + 6000; t += 50) {
        CRGB out[Board::LED_COUNT];
        fillPixels(out, Board::LED_COUNT, CRGB(1, 2, 3));   // sentinel: SKIP slots must keep it
        smoke.render(t, out);
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (isSkip(i)) {
                CHECK_RGB(CRGB(1, 2, 3), out[i], "t=%u SKIP slot %d written", t, i);
                continue;
            }
            CHECK(out[i].r == out[i].g && out[i].g == out[i].b, "t=%u slot %d not grey (%d,%d,%d)", t, i, out[i].r, out[i].g, out[i].b);
            CHECK(out[i].r <= LedSmokeAnimation::PEAK, "t=%u slot %d level %d above PEAK", t, i, out[i].r);
            if (out[i].r) lit++;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(lit > 0, "smoke never lit anything over 6 s");
}

static void test_smoke_deterministic_for_same_nowMs() {
    LedSmokeAnimation smoke;
    smoke.start(1000);
    CRGB out1[Board::LED_COUNT], out2[Board::LED_COUNT];
    fillPixels(out1, Board::LED_COUNT, CRGB(0, 0, 0));
    fillPixels(out2, Board::LED_COUNT, CRGB(0, 0, 0));
    smoke.render(1500, out1);
    smoke.render(1500, out2);
    for (int i = 0; i < Board::LED_COUNT; i++) CHECK_RGB(out1[i], out2[i], "slot %d not deterministic", i);

    // Only elapsed time matters: the same offset from a different start is the same frame.
    LedSmokeAnimation later;
    later.start(9000);
    CRGB out3[Board::LED_COUNT];
    fillPixels(out3, Board::LED_COUNT, CRGB(0, 0, 0));
    later.render(9500, out3);
    for (int i = 0; i < Board::LED_COUNT; i++) CHECK_RGB(out1[i], out3[i], "slot %d depends on more than elapsed time", i);
}

static void test_smoke_moves_over_time() {
    LedSmokeAnimation smoke;
    smoke.start(0);
    CRGB a[Board::LED_COUNT], b[Board::LED_COUNT];
    int changedFrames = 0;
    for (uint32_t t = 0; t < 4000; t += 100) {
        fillPixels(a, Board::LED_COUNT, CRGB(0, 0, 0));
        fillPixels(b, Board::LED_COUNT, CRGB(0, 0, 0));
        smoke.render(t, a);
        smoke.render(t + 100, b);
        bool differ = false;
        for (int i = 0; i < Board::LED_COUNT; i++) differ = differ || !(a[i] == b[i]);
        if (differ) changedFrames++;
    }
    CHECK(changedFrames >= 30, "only %d of 40 100 ms steps changed the smoke", changedFrames);
}

// The per-frame path is integer: smoothstep hits both ends exactly, is monotone,
// and value noise never jumps more than a few levels per 1/256 cell.
static void test_smoke_integer_noise_is_smooth() {
    TEST_ASSERT_EQUAL_UINT8(0, LedSmokeAnimation::smooth(0));
    TEST_ASSERT_EQUAL_UINT8(255, LedSmokeAnimation::smooth(255));
    for (int t = 1; t < 256; t++) {
        CHECK(LedSmokeAnimation::smooth(t) >= LedSmokeAnimation::smooth(t - 1), "smooth not monotone at %d", t);
    }
    for (uint32_t row = 0; row < 4; row++) {
        const uint32_t fy = (100u + row) * 256u + row * 61u;
        int prev = LedSmokeAnimation::sample(100u * 256u, fy, 7u);
        for (uint32_t fx = 100u * 256u + 1; fx < 108u * 256u; fx++) {
            const int v = LedSmokeAnimation::sample(fx, fy, 7u);
            CHECK(v - prev <= 3 && prev - v <= 3, "noise jumps %d -> %d at fx=%u", prev, v, fx);
            prev = v;
        }
    }
}

static void test_smoke_inactive_after_stop() {
    LedSmokeAnimation smoke;
    smoke.start(1000);
    smoke.stop();
    TEST_ASSERT_FALSE_MESSAGE(smoke.active(1500), "active after stop()");
}

static void test_on_fire_targets_per_side() {
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, LedDisplay::onFireTargets(false, false), "nobody");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(LedTarget::LeftScore | LedTarget::BarLeft, LedDisplay::onFireTargets(true, false), "left");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(LedTarget::RightScore | LedTarget::BarRight, LedDisplay::onFireTargets(false, true), "right");
}

// Only the fired side's lit digit and own-bar slots may change: everything else (unlit,
// untargeted, SKIP/dead - which carry no element bit at all) stays byte-identical.
static void test_on_fire_only_touches_lit_target_slots() {
    const GameScoreHistory history = ownerCheckHistory();

    CRGB bufferBase[Board::LED_COUNT];
    LedDisplay base(bufferBase);
    gameBallScreen(base, history, false);

    CRGB bufferFire[Board::LED_COUNT];
    LedDisplay onFire(bufferFire);
    gameBallScreen(onFire, history, false);
    g_fakeMillis = 5000;
    onFire.setOnFire(LedDisplay::onFireTargets(true, false));

    bool sawSmoke = false, hasLeftBar = false, sawBarSmoke = false;
    for (uint32_t t = 5000; t < 5000 + 4000; t += 100) {
        CRGB baseline[Board::LED_COUNT], fired[Board::LED_COUNT];
        renderAt(base, bufferBase, t, baseline);
        renderAt(onFire, bufferFire, t, fired);
        for (int slot = 0; slot < Board::LED_COUNT; slot++) {
            const uint16_t bits = onFire.elementsAt(slot);
            const bool isLeftTarget = (bits & (LedTarget::LeftScore | LedTarget::BarLeft)) != 0;
            const bool lit = baseline[slot].r | baseline[slot].g | baseline[slot].b;
            if (isLeftTarget && lit) {   // the only slots the smoke may touch - and Screen only brightens
                CHECK(fired[slot].r >= baseline[slot].r && fired[slot].g >= baseline[slot].g && fired[slot].b >= baseline[slot].b,
                      "t=%u slot %d darkened by Screen smoke", t, slot);
                const bool changed = !(fired[slot] == baseline[slot]);
                sawSmoke = sawSmoke || changed;
                if (bits & LedTarget::BarLeft) {
                    hasLeftBar = true;
                    sawBarSmoke = sawBarSmoke || changed;
                }
                continue;
            }
            CHECK_RGB(baseline[slot], fired[slot], "t=%u slot %d bits 0x%x changed outside the smoke target", t, slot, bits);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(sawSmoke, "smoke never reached the left digits in 4 s");
    // V1 only: the left player's lit bar pixels smoke too (V2 has no bar).
    if (hasLeftBar) TEST_ASSERT_TRUE_MESSAGE(sawBarSmoke, "smoke never reached the left bar pixels in 4 s");
}

// setOnFire(0) is a no-op unless the smoke itself owns LAYER_2: proves the
// hazard guard from #59's spec (never clobber a celebration or intro).
static void test_on_fire_stop_leaves_celebration_alone() {
    CRGB buffer[Board::LED_COUNT];
    LedDisplay display(buffer);
    display.setNumericValue(0, 0);
    display.setGlyphsAppearance(Colors::Orange, Colors::Aqua);
    display.setBorderEnabled(true);
    display.setBorderAppearance(Colors::Orange, Colors::Aqua);

    g_fakeMillis = 1000;
    display.startCelebration(Colors::White, true);
    CRGB withCelebration[Board::LED_COUNT];
    renderAt(display, buffer, 1000, withCelebration);

    display.setOnFire(0);   // the smoke was never started, so it does not own LAYER_2
    CRGB afterOnFireStop[Board::LED_COUNT];
    renderAt(display, buffer, 1000, afterOnFireStop);

    for (int i = 0; i < Board::LED_COUNT; i++) {
        CHECK_RGB(withCelebration[i], afterOnFireStop[i], "slot %d: setOnFire(0) disturbed the celebration frame", i);
    }
    TEST_ASSERT_TRUE_MESSAGE(display.celebrationActive(), "celebration stopped by setOnFire(0)");
}

static void test_on_fire_stop_leaves_intro_alone() {
    CRGB buffer[Board::LED_COUNT];
    LedDisplay display(buffer);
    display.setBorderEnabled(true);

    g_fakeMillis = 2000;
    display.startIntro(Colors::Orange, Colors::Aqua);
    CRGB withIntro[Board::LED_COUNT];
    renderAt(display, buffer, 2000, withIntro);

    display.setOnFire(0);   // the smoke was never started, so it does not own LAYER_2
    CRGB afterOnFireStop[Board::LED_COUNT];
    renderAt(display, buffer, 2000, afterOnFireStop);

    for (int i = 0; i < Board::LED_COUNT; i++) {
        CHECK_RGB(withIntro[i], afterOnFireStop[i], "slot %d: setOnFire(0) disturbed the intro frame", i);
    }
    TEST_ASSERT_TRUE_MESSAGE(display.introActive(), "intro stopped by setOnFire(0)");
}

// The positive case of the same guard: when the smoke DOES own LAYER_2, setOnFire(0)
// must release it, verified against a display that never started the smoke at all.
static void test_on_fire_stop_releases_layer_it_owns() {
    CRGB bufferA[Board::LED_COUNT];
    LedDisplay noSmoke(bufferA);
    noSmoke.setNumericValue(0, 0);
    noSmoke.setGlyphsAppearance(Colors::Orange, Colors::Aqua);
    CRGB baseline[Board::LED_COUNT];
    renderAt(noSmoke, bufferA, 3000, baseline);

    CRGB bufferB[Board::LED_COUNT];
    LedDisplay display(bufferB);
    display.setNumericValue(0, 0);
    display.setGlyphsAppearance(Colors::Orange, Colors::Aqua);

    g_fakeMillis = 3000;
    display.setOnFire(LedDisplay::onFireTargets(true, false));
    display.setOnFire(0);   // the smoke owns LAYER_2 here, so this must clear it
    CRGB afterStop[Board::LED_COUNT];
    renderAt(display, bufferB, 3000, afterStop);

    for (int i = 0; i < Board::LED_COUNT; i++) {
        CHECK_RGB(baseline[i], afterStop[i], "slot %d: setOnFire(0) failed to release LAYER_2 it owned", i);
    }
}

static void test_on_fire_reset_animations_stops_it() {
    CRGB bufferA[Board::LED_COUNT];
    LedDisplay noSmoke(bufferA);
    noSmoke.setNumericValue(0, 0);
    noSmoke.setGlyphsAppearance(Colors::Orange, Colors::Aqua);
    CRGB baseline[Board::LED_COUNT];
    renderAt(noSmoke, bufferA, 4000, baseline);

    CRGB bufferB[Board::LED_COUNT];
    LedDisplay display(bufferB);
    display.setNumericValue(0, 0);
    display.setGlyphsAppearance(Colors::Orange, Colors::Aqua);

    g_fakeMillis = 4000;
    display.setOnFire(LedDisplay::onFireTargets(true, false));
    display.resetAnimations();
    CRGB afterReset[Board::LED_COUNT];
    renderAt(display, bufferB, 4000, afterReset);

    for (int i = 0; i < Board::LED_COUNT; i++) {
        CHECK_RGB(baseline[i], afterReset[i], "slot %d: resetAnimations left the smoke active", i);
    }
}

// Game ball outranks on fire (views: onFireTargets(isOnFire && !breathe), breathing
// unmasked). Both sides on fire, left at game ball: the left breathes exactly as
// with no smoke at all, the right carries smoke only.
static void test_game_ball_wins_over_on_fire() {
    const GameScoreHistory history = ownerCheckHistory();
    const uint32_t started = 1000;
    const uint32_t t = started + 700;   // breathing's trough: its dimming is unambiguous

    const bool breatheA = true, breatheB = false;
    const uint16_t fire = LedDisplay::onFireTargets(!breatheA, !breatheB);   // both sides on fire
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(LedTarget::RightScore | LedTarget::BarRight, fire, "the breathing side's smoke is off");

    CRGB buffer[Board::LED_COUNT];
    LedDisplay display(buffer);
    gameBallScreen(display, history, false);
    g_fakeMillis = started;
    display.setOnFire(fire);
    display.setBreathing(LedDisplay::breathingTargets(breatheA, breatheB));
    CRGB combo[Board::LED_COUNT];
    renderAt(display, buffer, t, combo);

    CRGB bufferBreathe[Board::LED_COUNT];
    LedDisplay breatheOnly(bufferBreathe);
    gameBallScreen(breatheOnly, history, false);
    g_fakeMillis = started;
    breatheOnly.setBreathing(LedDisplay::breathingTargets(breatheA, breatheB));
    CRGB breathed[Board::LED_COUNT];
    renderAt(breatheOnly, bufferBreathe, t, breathed);

    CRGB bufferSmoke[Board::LED_COUNT];
    LedDisplay smokeOnly(bufferSmoke);
    gameBallScreen(smokeOnly, history, false);
    g_fakeMillis = started;
    smokeOnly.setOnFire(fire);
    CRGB smoked[Board::LED_COUNT];
    renderAt(smokeOnly, bufferSmoke, t, smoked);

    CRGB bufferNeither[Board::LED_COUNT];
    LedDisplay neither(bufferNeither);
    gameBallScreen(neither, history, false);
    CRGB plain[Board::LED_COUNT];
    renderAt(neither, bufferNeither, t, plain);

    bool leftDigitDimmed = false;
    for (int slot = 0; slot < Board::LED_COUNT; slot++) {
        const uint16_t bits = display.elementsAt(slot);
        if (bits & (LedTarget::RightScore | LedTarget::BarRight)) {
            CHECK_RGB(smoked[slot], combo[slot], "slot %d: right digits and bar must carry the smoke only", slot);
        } else {
            CHECK_RGB(breathed[slot], combo[slot], "slot %d: breathing side must look as if no smoke ran", slot);
            if ((bits & LedTarget::LeftScore) && !(combo[slot] == plain[slot])) leftDigitDimmed = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(leftDigitDimmed, "left digits never dimmed - breathing masked off the game-ball side");
}

// -------------------------------------------------------- comeback (#60) vs fire ---
// LAYER_2 is shared by the smoke (#59) and the comeback burst during GamePlaying;
// startComeback() stops the smoke and claims it, setOnFire() must yield while the
// burst runs, and the smoke must be free to retake the layer once it ends.

static void test_comeback_during_fire_wins_layer_and_smoke_resumes_after() {
    // References: smoke alone and comeback alone, each on its own untouched display.
    CRGB bufferSmoke[Board::LED_COUNT];
    LedDisplay smokeOnly(bufferSmoke);
    smokeOnly.setNumericValue(0, 0);
    smokeOnly.setGlyphsAppearance(Colors::Orange, Colors::Aqua);
    g_fakeMillis = 1000;
    smokeOnly.setOnFire(LedDisplay::onFireTargets(true, false));
    CRGB smokeFrame[Board::LED_COUNT];
    renderAt(smokeOnly, bufferSmoke, 1000, smokeFrame);

    CRGB bufferComeback[Board::LED_COUNT];
    LedDisplay comebackOnly(bufferComeback);
    comebackOnly.setNumericValue(0, 0);
    comebackOnly.setGlyphsAppearance(Colors::Orange, Colors::Aqua);
    g_fakeMillis = 1000;
    comebackOnly.startComeback(Colors::White, true);
    CRGB comebackFrame[Board::LED_COUNT];
    renderAt(comebackOnly, bufferComeback, 1000, comebackFrame);

    bool differ = false;
    for (int i = 0; i < Board::LED_COUNT; i++) differ = differ || !(smokeFrame[i] == comebackFrame[i]);
    TEST_ASSERT_TRUE_MESSAGE(differ, "sanity: smoke-only and comeback-only frames must differ");

    // Fire first, then a comeback lands on top of it.
    CRGB buffer[Board::LED_COUNT];
    LedDisplay display(buffer);
    display.setNumericValue(0, 0);
    display.setGlyphsAppearance(Colors::Orange, Colors::Aqua);

    g_fakeMillis = 1000;
    display.setOnFire(LedDisplay::onFireTargets(true, false));   // smoke owns LAYER_2
    display.startComeback(Colors::White, true);                  // takes it over for 800 ms

    CRGB duringComeback[Board::LED_COUNT];
    renderAt(display, buffer, 1000, duringComeback);
    for (int i = 0; i < Board::LED_COUNT; i++) {
        CHECK_RGB(comebackFrame[i], duringComeback[i], "slot %d: comeback did not win LAYER_2 over the smoke", i);
    }

    // Every frame the view still calls setOnFire(fire) first; it must not reclaim the layer.
    display.setOnFire(LedDisplay::onFireTargets(true, false));
    CRGB stillComeback[Board::LED_COUNT];
    renderAt(display, buffer, 1000, stillComeback);
    for (int i = 0; i < Board::LED_COUNT; i++) {
        CHECK_RGB(comebackFrame[i], stillComeback[i], "slot %d: setOnFire(fire) overrode a running comeback burst", i);
    }

    // Past the single 800 ms cycle the sweep reports inactive: the smoke must be
    // able to retake LAYER_2 on the very next setOnFire(), not be blocked forever.
    const uint32_t after = 1000 + LedSweepAnimation::comebackParams().durationMs + 10;

    CRGB bufferFreshSmoke[Board::LED_COUNT];
    LedDisplay freshSmoke(bufferFreshSmoke);
    freshSmoke.setNumericValue(0, 0);
    freshSmoke.setGlyphsAppearance(Colors::Orange, Colors::Aqua);
    g_fakeMillis = after;
    freshSmoke.setOnFire(LedDisplay::onFireTargets(true, false));
    CRGB freshSmokeFrame[Board::LED_COUNT];
    renderAt(freshSmoke, bufferFreshSmoke, after, freshSmokeFrame);

    g_fakeMillis = after;
    display.setOnFire(LedDisplay::onFireTargets(true, false));
    CRGB resumedFrame[Board::LED_COUNT];
    renderAt(display, buffer, after, resumedFrame);
    for (int i = 0; i < Board::LED_COUNT; i++) {
        CHECK_RGB(freshSmokeFrame[i], resumedFrame[i], "slot %d: smoke failed to retake LAYER_2 once the burst ended", i);
    }
}

// setOnFire(0) while a comeback is running must leave the sweep alone - mirrors
// the celebration/intro guards, this time on the other side of the yield check.
static void test_on_fire_stop_leaves_comeback_alone() {
    CRGB buffer[Board::LED_COUNT];
    LedDisplay display(buffer);
    display.setNumericValue(0, 0);
    display.setGlyphsAppearance(Colors::Orange, Colors::Aqua);

    g_fakeMillis = 2000;
    display.startComeback(Colors::White, true);
    CRGB withComeback[Board::LED_COUNT];
    renderAt(display, buffer, 2000, withComeback);

    display.setOnFire(0);   // the comeback owns LAYER_2 here, not the smoke
    CRGB afterOnFireStop[Board::LED_COUNT];
    renderAt(display, buffer, 2000, afterOnFireStop);

    for (int i = 0; i < Board::LED_COUNT; i++) {
        CHECK_RGB(withComeback[i], afterOnFireStop[i], "slot %d: setOnFire(0) disturbed a running comeback burst", i);
    }
}

// No fire ever involved: a comeback must render exactly like the bare sweep
// (base screen Normal-blended with the ring), proving startComeback()'s
// unconditional smoke.stop() has no visible side effect when the smoke was
// already idle.
static void test_comeback_with_no_fire_matches_plain_sweep() {
    CRGB buffer[Board::LED_COUNT];
    LedDisplay display(buffer);
    display.setNumericValue(0, 0);
    display.setGlyphsAppearance(Colors::Orange, Colors::Aqua);

    CRGB bufferPlain[Board::LED_COUNT];
    LedDisplay plain(bufferPlain);
    plain.setNumericValue(0, 0);
    plain.setGlyphsAppearance(Colors::Orange, Colors::Aqua);

    g_fakeMillis = 3000;
    display.startComeback(Colors::White, true);

    LedSweepAnimation ring(LedSweepAnimation::comebackParams());
    ring.setSolidColor(CRGB(255, 255, 255));   // matches Colors::White passed to startComeback below
    ring.setOriginToHalf(true);
    ring.start(3000);

    const uint32_t t = 3050;
    CRGB withComeback[Board::LED_COUNT], base[Board::LED_COUNT], ringFrame[Board::LED_COUNT];
    fillPixels(ringFrame, Board::LED_COUNT, CRGB(0, 0, 0));
    renderAt(display, buffer, t, withComeback);
    renderAt(plain, bufferPlain, t, base);
    ring.render(t, ringFrame);

    for (int i = 0; i < Board::LED_COUNT; i++) {
        CHECK_RGB(blendPixel(base[i], ringFrame[i], BlendMode::Normal), withComeback[i], "slot %d", i);
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
    RUN_TEST(test_smoke_is_neutral_grey_and_skips_dead_slots);
    RUN_TEST(test_smoke_deterministic_for_same_nowMs);
    RUN_TEST(test_smoke_moves_over_time);
    RUN_TEST(test_smoke_integer_noise_is_smooth);
    RUN_TEST(test_smoke_inactive_after_stop);
    RUN_TEST(test_on_fire_targets_per_side);
    RUN_TEST(test_on_fire_only_touches_lit_target_slots);
    RUN_TEST(test_on_fire_stop_leaves_celebration_alone);
    RUN_TEST(test_on_fire_stop_leaves_intro_alone);
    RUN_TEST(test_on_fire_stop_releases_layer_it_owns);
    RUN_TEST(test_on_fire_reset_animations_stops_it);
    RUN_TEST(test_game_ball_wins_over_on_fire);
    RUN_TEST(test_comeback_during_fire_wins_layer_and_smoke_resumes_after);
    RUN_TEST(test_on_fire_stop_leaves_comeback_alone);
    RUN_TEST(test_comeback_with_no_fire_matches_plain_sweep);
    return UNITY_END();
}
