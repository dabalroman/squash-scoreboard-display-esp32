// Board-agnostic invariants for the LED layer engine (task #52): blend maths,
// the slot -> element map, LedLayerStack::compose() and the breathing animation.
// Shared by check_v1.cpp and check_v2.cpp.
#ifndef LED_DUMP_LAYER_CHECKS_H
#define LED_DUMP_LAYER_CHECKS_H

#include <cstdio>

#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Animation/LedSlotPositions.h"
#include "Display/LedDisplay/Layers/LedBlend.h"
#include "Display/LedDisplay/Layers/LedBreathingAnimation.h"
#include "Display/LedDisplay/Layers/LedLayerStack.h"
#include "Display/LedDisplay/Layers/LedTarget.h"

struct LayerCheckConfig {
    const char *boardName;
    int indicatorA;   // slot of IndicatorPlayerA
    int indicatorB;
    int colonSlots;   // expected slot counts per element (0 = the board has none)
    int barSlots;
    int borderTopSlots;
    int borderBottomSlots;
};

// Reference formulas in plain int maths, independent of LedBlend's div255 trick.
static int layerRefChannel(const BlendMode mode, const int a, const int b, const int alpha) {
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

static const BlendMode LAYER_ALL_MODES[] = {
    BlendMode::Normal, BlendMode::Screen, BlendMode::Add, BlendMode::Lighten, BlendMode::Multiply,
};

static int runBlendChecks(const char *board) {
    int failures = 0;

    for (int x = 0; x <= 255 * 255; x++) {
        if (LedBlend::div255(static_cast<uint16_t>(x)) != x / 255) {
            printf("FAIL %s div255(%d)\n", board, x);
            failures++;
            break;
        }
    }

    // Exhaustive over grey pairs: every channel sees every (a, b), and alpha = b.
    for (const BlendMode mode : LAYER_ALL_MODES) {
        int modeFailures = 0;
        for (int a = 0; a < 256; a++) {
            for (int b = 0; b < 256; b++) {
                const CRGB out = blendPixel(CRGB(a, a, a), CRGB(b, b, b), mode);
                const int expected = layerRefChannel(mode, a, b, b);
                const int o = out.r;
                bool bad = out.r != expected || out.g != expected || out.b != expected;

                if (mode == BlendMode::Lighten && (o < a || o < b)) bad = true;
                if (mode == BlendMode::Screen && (o < a || o < b)) bad = true;
                if (mode == BlendMode::Add && o < a) bad = true;
                if (mode == BlendMode::Multiply && (o > a || o > b)) bad = true;
                if (mode == BlendMode::Normal && o < b) bad = true;

                if (bad && modeFailures++ < 3) {
                    printf("FAIL %s blend %s a=%d b=%d -> %d expected %d\n", board, blendName(mode), a, b, o, expected);
                }
            }
        }
        failures += modeFailures;
    }

    // Identity colours on coloured bases, where Normal's alpha is not the channel itself.
    const uint8_t levels[] = {0, 1, 17, 64, 127, 128, 200, 254, 255};
    for (const BlendMode mode : LAYER_ALL_MODES) {
        for (const uint8_t r : levels) {
            for (const uint8_t g : levels) {
                for (const uint8_t b : levels) {
                    const CRGB base(r, g, b);
                    if (!(blendPixel(base, identityFor(mode), mode) == base)) {
                        printf("FAIL %s blend %s identity changes (%d,%d,%d)\n", board, blendName(mode), r, g, b);
                        failures++;
                    }
                    // Normal with a fully bright channel is opaque: the layer replaces the base.
                    const CRGB opaque(255, g, b);
                    if (mode == BlendMode::Normal && !(blendPixel(base, opaque, mode) == opaque)) {
                        printf("FAIL %s blend Normal not opaque at alpha 255\n", board);
                        failures++;
                    }
                }
            }
        }
    }

    return failures;
}

static int runElementMapChecks(const LayerCheckConfig &cfg) {
    int failures = 0;
    CRGB buffer[Board::LED_COUNT];
    LedDisplay display(buffer);

    // Each glyph position's bit covers exactly its segment table's slots.
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
            const bool marked = (display.elementsAt(slot) & glyph.bit) != 0;
            if (marked != inTable[slot]) {
                printf("FAIL %s element map bit 0x%x slot %d marked=%d table=%d\n",
                       cfg.boardName, glyph.bit, slot, marked, inTable[slot]);
                failures++;
            }
        }
    }

    int colon = 0, bar = 0, top = 0, bottom = 0;
    for (int slot = 0; slot < Board::LED_COUNT; slot++) {
        const uint16_t bits = display.elementsAt(slot);

        if (bits & (bits - 1)) {
            printf("FAIL %s slot %d in two elements (0x%x)\n", cfg.boardName, slot, bits);
            failures++;
        }

        // Front must be exactly the slots with a front-facing die.
        const bool front = (bits & LedTarget::Front) != 0;
        const bool live = LedSlots::POS[slot][0] != LedSlots::SKIP;
        if (front != live) {
            printf("FAIL %s slot %d front=%d but live=%d\n", cfg.boardName, slot, front, live);
            failures++;
        }

        if (bits & LedTarget::Colon) colon++;
        if (bits & LedTarget::Bar) bar++;
        if (bits & LedTarget::BorderTop) top++;
        if (bits & LedTarget::BorderBottom) bottom++;
    }

    if (display.elementsAt(cfg.indicatorA) != LedTarget::IndicatorA
        || display.elementsAt(cfg.indicatorB) != LedTarget::IndicatorB) {
        printf("FAIL %s indicator slots not their own element\n", cfg.boardName);
        failures++;
    }
    if (colon != cfg.colonSlots || bar != cfg.barSlots || top != cfg.borderTopSlots || bottom != cfg.borderBottomSlots) {
        printf("FAIL %s element counts colon=%d bar=%d top=%d bottom=%d\n", cfg.boardName, colon, bar, top, bottom);
        failures++;
    }

    return failures;
}

// Fills every slot with one colour; active only while `on`.
class LayerCheckFill : public LedAnimation {
public:
    CRGB color;
    bool on;
    bool writes;

    LayerCheckFill(const CRGB color, const bool on, const bool writes = true) : color(color), on(on), writes(writes) {}

    bool active(uint32_t) const override { return on; }

    void render(uint32_t, CRGB *out) const override {
        if (!writes) return;
        for (int i = 0; i < Board::LED_COUNT; i++) out[i] = color;
    }
};

static int runComposeChecks(const char *board) {
    int failures = 0;
    uint16_t map[Board::LED_COUNT];
    CRGB base[Board::LED_COUNT];
    CRGB pixels[Board::LED_COUNT];

    // Even slots digit A, odd slots digit B; every third slot lit.
    for (int i = 0; i < Board::LED_COUNT; i++) {
        map[i] = i % 2 ? LedTarget::DigitB : LedTarget::DigitA;
        base[i] = i % 3 == 0 ? CRGB(40, 80, 120) : CRGB(0, 0, 0);
    }
    auto reset = [&]() { for (int i = 0; i < Board::LED_COUNT; i++) pixels[i] = base[i]; };
    auto unchanged = [&](const char *what) {
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (!(pixels[i] == base[i])) {
                printf("FAIL %s compose %s changed slot %d\n", board, what, i);
                return 1;
            }
        }
        return 0;
    };

    {
        LedLayerStack stack(map);
        reset();
        stack.compose(pixels, 0);
        failures += unchanged("empty stack");
    }
    {
        LayerCheckFill white(CRGB(255, 255, 255), false);
        LedLayerStack stack(map);
        stack.set(LedLayerStack::LAYER_2, &white, BlendMode::Add, LedTarget::All, LayerMask::AllSlots);
        reset();
        stack.compose(pixels, 0);
        failures += unchanged("inactive layer");
    }
    {
        // Multiply with nothing written: the white identity fill must leave the base alone.
        LayerCheckFill silent(CRGB(0, 0, 0), true, false);
        LedLayerStack stack(map);
        stack.set(LedLayerStack::LAYER_1, &silent, BlendMode::Multiply, LedTarget::All, LayerMask::AllSlots);
        reset();
        stack.compose(pixels, 0);
        failures += unchanged("multiply identity fill");
    }
    {
        // Target: only digit A's slots may change.
        LayerCheckFill white(CRGB(255, 255, 255), true);
        LedLayerStack stack(map);
        stack.set(LedLayerStack::LAYER_2, &white, BlendMode::Add, LedTarget::DigitA, LayerMask::AllSlots);
        reset();
        stack.compose(pixels, 0);
        for (int i = 0; i < Board::LED_COUNT; i++) {
            const CRGB expected = i % 2 ? base[i] : CRGB(255, 255, 255);
            if (!(pixels[i] == expected)) {
                printf("FAIL %s compose target slot %d\n", board, i);
                failures++;
            }
        }
    }
    {
        // LitOnly never touches a slot the base left dark.
        LayerCheckFill white(CRGB(255, 255, 255), true);
        LedLayerStack stack(map);
        stack.set(LedLayerStack::LAYER_1, &white, BlendMode::Add, LedTarget::All, LayerMask::LitOnly);
        reset();
        stack.compose(pixels, 0);
        for (int i = 0; i < Board::LED_COUNT; i++) {
            const CRGB expected = i % 3 == 0 ? CRGB(255, 255, 255) : CRGB(0, 0, 0);
            if (!(pixels[i] == expected)) {
                printf("FAIL %s compose LitOnly slot %d\n", board, i);
                failures++;
            }
        }
    }
    {
        // Order and capture: layer 1 lights everything, layer 2 (LitOnly) sees the
        // base's lit set, not layer 1's output.
        LayerCheckFill red(CRGB(100, 0, 0), true);
        LayerCheckFill half(CRGB(128, 128, 128), true);
        LedLayerStack stack(map);
        stack.set(LedLayerStack::LAYER_2, &half, BlendMode::Multiply, LedTarget::All, LayerMask::LitOnly);
        stack.set(LedLayerStack::LAYER_1, &red, BlendMode::Add, LedTarget::All, LayerMask::AllSlots);
        reset();
        stack.compose(pixels, 0);
        for (int i = 0; i < Board::LED_COUNT; i++) {
            const CRGB added = blendPixel(base[i], CRGB(100, 0, 0), BlendMode::Add);
            const CRGB expected = i % 3 == 0 ? blendPixel(added, CRGB(128, 128, 128), BlendMode::Multiply) : added;
            if (!(pixels[i] == expected)) {
                printf("FAIL %s compose order/capture slot %d\n", board, i);
                failures++;
            }
        }
    }

    return failures;
}

static int runBreathingChecks(const char *board) {
    int failures = 0;
    const uint8_t minLevel = 102;
    LedBreathingAnimation breathing(2000, minLevel);
    breathing.start(1000);

    int lowest = 255, highest = 0;
    for (uint32_t t = 1000; t < 1000 + 4000; t++) {
        const int level = breathing.levelAt(t);
        if (level < lowest) lowest = level;
        if (level > highest) highest = level;
        if (level < minLevel) {
            printf("FAIL %s breathing t=%u level %d below min\n", board, t, level);
            failures++;
            break;
        }
    }
    if (lowest != minLevel || highest != 255) {
        printf("FAIL %s breathing range [%d, %d], expected [%d, 255]\n", board, lowest, highest, minLevel);
        failures++;
    }

    CRGB out[Board::LED_COUNT];
    breathing.render(1500, out);
    const uint8_t level = breathing.levelAt(1500);
    for (int i = 0; i < Board::LED_COUNT; i++) {
        if (!(out[i] == CRGB(level, level, level))) {
            printf("FAIL %s breathing slot %d not grey %d\n", board, i, level);
            failures++;
            break;
        }
    }

    breathing.stop();
    if (breathing.active(1500)) {
        printf("FAIL %s breathing active after stop()\n", board);
        failures++;
    }

    return failures;
}

static int runLayerChecks(const LayerCheckConfig &cfg) {
    const int blend = runBlendChecks(cfg.boardName);
    const int map = runElementMapChecks(cfg);
    const int compose = runComposeChecks(cfg.boardName);
    const int breathing = runBreathingChecks(cfg.boardName);
    printf("layers %s: blend %d, element map %d, compose %d, breathing %d failures\n",
           cfg.boardName, blend, map, compose, breathing);
    return blend + map + compose + breathing;
}

#endif //LED_DUMP_LAYER_CHECKS_H
