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
#include "Display/LedDisplay/Renderer/GameScoreHistoryBarRenderer.h"

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

// Bar-owner fixtures (task #55), shared by the element map and game-ball checks.

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

static std::array<LedBarPixel, LedBar::PIXEL_COUNT> ownerCheckPixels(const GameScoreHistory &history) {
    return GameScoreHistoryBarRenderer::toLedBarPixels(Colors::Red, Colors::Blue, history, 1, 2);
}

static int runElementMapChecks(const LayerCheckConfig &cfg) {
    int failures = 0;
    CRGB buffer[Board::LED_COUNT];
    LedDisplay display(buffer);
    // With owners marked, so the one-element test below must see past them.
    const GameScoreHistory history = ownerCheckHistory();
    display.setLedBarState([&] { return ownerCheckPixels(history); });

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
        // Owner bits ride on Bar slots on top of the element bit (task #55).
        const uint16_t bits = display.elementsAt(slot) & static_cast<uint16_t>(~(LedTarget::BarLeft | LedTarget::BarRight));

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

// Owner bits must equal the pixels' sides on Bar slots (in slot order) and sit nowhere else.
static int checkOwners(const char *board, const char *what, const LedDisplay &display,
                       const std::array<LedBarPixel, LedBar::PIXEL_COUNT> *pixels) {
    int failures = 0;
    int barIndex = 0;
    for (int slot = 0; slot < Board::LED_COUNT; slot++) {
        const uint16_t bits = display.elementsAt(slot);
        uint16_t expected = 0;
        if (bits & LedTarget::Bar) {
            const GameSide side = pixels ? (*pixels)[barIndex].side : GameSide::none;
            expected = side == GameSide::a ? LedTarget::BarLeft : side == GameSide::b ? LedTarget::BarRight : 0;
            barIndex++;
        }
        if ((bits & OWNER_BITS) != expected) {
            printf("FAIL %s owners %s slot %d bits 0x%x expected owner 0x%x\n", board, what, slot, bits, expected);
            failures++;
        }
    }
    return failures;
}

static int runOwnerChecks(const LayerCheckConfig &cfg) {
    int failures = 0;
    CRGB buffer[Board::LED_COUNT];
    LedDisplay display(buffer);

    failures += checkOwners(cfg.boardName, "fresh", display, nullptr);

    // Point width 2, padding 1: four points -> 4 left and 4 right pixels.
    const GameScoreHistory mixed = ownerCheckHistory();
    const auto mixedPixels = ownerCheckPixels(mixed);
    int left = 0, right = 0;
    for (const LedBarPixel &p : mixedPixels) {
        left += p.side == GameSide::a;
        right += p.side == GameSide::b;
    }
    if (left != 4 || right != 4) {
        printf("FAIL %s owners renderer sides left=%d right=%d, expected 4/4\n", cfg.boardName, left, right);
        failures++;
    }

    display.setLedBarState([&] { return ownerCheckPixels(mixed); });
    // V2 has no bar: its setter runs nothing, so no slot may carry an owner.
    failures += checkOwners(cfg.boardName, "mixed", display, cfg.barSlots ? &mixedPixels : nullptr);

    // A new state re-marks every slot: nothing left over from the mixed one.
    GameScoreHistory onlyB;
    onlyB.scorePoint(GameSide::b);
    onlyB.commit();
    const auto onlyBPixels = ownerCheckPixels(onlyB);
    display.setLedBarState([&] { return ownerCheckPixels(onlyB); });
    failures += checkOwners(cfg.boardName, "new state", display, cfg.barSlots ? &onlyBPixels : nullptr);

    display.setLedBarState([&] { return ownerCheckPixels(mixed); });
    display.resetAnimations();
    failures += checkOwners(cfg.boardName, "after resetAnimations", display, nullptr);

    // Every other bar renderer leaves the owner at none.
    const LedBarPixel plain;
    if (plain.side != GameSide::none) {
        printf("FAIL %s owners LedBarPixel default side not none\n", cfg.boardName);
        failures++;
    }

    return failures;
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
    for (int i = 0; i < Board::LED_COUNT; i++) buffer[i] = CRGB(0, 0, 0);
    display.render();
    for (int i = 0; i < Board::LED_COUNT; i++) out[i] = buffer[i];
}

// Which slots a side's breathing may reach - bar ownership from the renderer's
// own pixels, not the element map, so a mis-marked owner cannot hide itself.
static bool inSide(const uint16_t bits, const int barIndex, const bool left,
                   const std::array<LedBarPixel, LedBar::PIXEL_COUNT> &bar) {
    if (bits & (left ? (LedTarget::LeftScore | LedTarget::BorderTop) : (LedTarget::RightScore | LedTarget::BorderBottom))) {
        return true;
    }
    return barIndex >= 0 && bar[barIndex].side == (left ? GameSide::a : GameSide::b);
}

static int runGameBallBreathingChecks(const LayerCheckConfig &cfg) {
    int failures = 0;
    const char *board = cfg.boardName;

    const uint16_t leftTargets = LedTarget::LeftScore | LedTarget::BorderTop | LedTarget::BarLeft;
    const uint16_t rightTargets = LedTarget::RightScore | LedTarget::BorderBottom | LedTarget::BarRight;
    if (LedDisplay::breathingTargets(false, false) != 0
        || LedDisplay::breathingTargets(true, false) != leftTargets
        || LedDisplay::breathingTargets(false, true) != rightTargets
        || LedDisplay::breathingTargets(true, true) != (leftTargets | rightTargets)) {
        printf("FAIL %s breathingTargets mapping\n", board);
        failures++;
    }

    const GameScoreHistory history = ownerCheckHistory();
    const auto bar = ownerCheckPixels(history);
    const uint32_t started = 1000;

    for (int side = 0; side < 2; side++) {
        const bool left = side == 0;
        const char *name = left ? "left" : "right";
        CRGB buffer[Board::LED_COUNT];
        CRGB breathed[Board::LED_COUNT];
        CRGB base[Board::LED_COUNT];
        LedDisplay display(buffer);
        gameBallScreen(display, history, false);

        const uint16_t mine = left ? LedDisplay::breathingTargets(true, false) : LedDisplay::breathingTargets(false, true);
        const uint16_t both = LedDisplay::breathingTargets(true, true);

        // Repeat calls, including one that moves the mask, keep the phase: 700 ms
        // in is the trough (102), where a restart would still read 255.
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
        if (level != 102) {
            printf("FAIL %s game ball trough level %d, expected 102\n", board, level);
            failures++;
        }

        int touched = 0, barTouched = 0, borderTouched = 0;
        int barIndex = 0;
        for (int slot = 0; slot < Board::LED_COUNT; slot++) {
            const uint16_t bits = display.elementsAt(slot);
            const int bi = (bits & LedTarget::Bar) ? barIndex++ : -1;
            const bool lit = base[slot].r | base[slot].g | base[slot].b;
            const bool dims = lit && inSide(bits, bi, left, bar);
            const CRGB expected = dims ? blendPixel(base[slot], CRGB(level, level, level), BlendMode::Multiply) : base[slot];
            if (!(breathed[slot] == expected)) {
                printf("FAIL %s game ball %s slot %d bits 0x%x (%d,%d,%d) expected (%d,%d,%d)\n",
                       board, name, slot, bits, breathed[slot].r, breathed[slot].g, breathed[slot].b,
                       expected.r, expected.g, expected.b);
                failures++;
            }
            if (dims) {
                touched++;
                if (bi >= 0) barTouched++;
                if (bits & (LedTarget::BorderTop | LedTarget::BorderBottom)) borderTouched++;
            }
        }
        // Only meaningful if every part of the side was lit and so actually dimmed.
        if (touched == 0 || (cfg.barSlots && barTouched == 0) || (cfg.borderTopSlots && borderTouched == 0)) {
            printf("FAIL %s game ball %s dimmed too little (touched %d bar %d border %d)\n",
                   board, name, touched, barTouched, borderTouched);
            failures++;
        }

        // Off -> on restarts at 255, so switching it on never jumps dark.
        g_fakeMillis = started + 700;
        display.setBreathing(mine);
        renderAt(display, buffer, started + 700, breathed);
        for (int slot = 0; slot < Board::LED_COUNT; slot++) {
            if (!(breathed[slot] == base[slot])) {
                printf("FAIL %s game ball %s restart not at full level, slot %d\n", board, name, slot);
                failures++;
                break;
            }
        }
    }

    // Blink + breathing on the left: the dark phase stays black, the lit phase is
    // the breathing-scaled colour, the right side never moves.
    {
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
                if (!(breathed[slot] == expected)) {
                    printf("FAIL %s blink+breathing t=%u slot %d\n", board, t, slot);
                    failures++;
                }
                if (leftDigit && lit) leftLit++;
            }
            // Otherwise the dark-phase case would hold trivially.
            if (dark != (leftLit == 0)) {
                printf("FAIL %s blink+breathing t=%u left digits lit %d\n", board, t, leftLit);
                failures++;
            }
        }
    }

    return failures;
}

static int runLayerChecks(const LayerCheckConfig &cfg) {
    const int blend = runBlendChecks(cfg.boardName);
    const int map = runElementMapChecks(cfg);
    const int compose = runComposeChecks(cfg.boardName);
    const int breathing = runBreathingChecks(cfg.boardName);
    const int owners = runOwnerChecks(cfg);
    const int gameBall = runGameBallBreathingChecks(cfg);
    printf("layers %s: blend %d, element map %d, compose %d, breathing %d, owners %d, game ball %d failures\n",
           cfg.boardName, blend, map, compose, breathing, owners, gameBall);
    return blend + map + compose + breathing + owners + gameBall;
}

#endif //LED_DUMP_LAYER_CHECKS_H
