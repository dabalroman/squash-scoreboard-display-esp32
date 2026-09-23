// V2 (BOARD_REV=2) invariants for the glyph layer, checked on the host:
//   - every write is inside the 74-slot chain
//   - dead slots 12/28/44/60 are never written
//   - digits stay inside their own module (A=58-73, B=42-57, C=26-41, D=10-25)
//     and never touch the centre block (border 0-3/5-8, indicators 4/9)
//   - indicator A writes only slot 9, B only slot 4; the colon writes nothing
//   - border and back indicators are enabled/coloured independently
//   - the boot sweep stays on front slots, ends dark, and never blanks a frame
//   - the celebration takes over the front: digits/colon/border sit out, indicators do not
//   - per-segment brightness compensation (task #43): the factor table matches the
//     measured-area formula, indicators/dead slots are untouched, and the worst-case
//     die-equivalent draw stays under the old flat-0.8 bound
// Build: ./check_v2.sh
// Boot-sweep and celebration invariants live in sweep_checks.h, shared with check_v1.cpp.
#include <cmath>
#include <cstdio>

#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Profiles/NineSegmentBrightness.h"
#include "sweep_checks.h"

uint32_t g_fakeMillis = 0;
CFastLED FastLED;

static const int GUARD = 256;   // oversized so out-of-range writes are caught

static bool v2SweepReserved(const int slot) {
    return slot == 4 || slot == 9 || slot == 12 || slot == 28 || slot == 44 || slot == 60;
}

static bool allowed(const int id, const int slot) {
    if (slot >= 74 || slot == 12 || slot == 28 || slot == 44 || slot == 60) {
        return false;
    }
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

int main() {
    const CRGB sentinel(1, 2, 3);
    int failures = 0;
    int writes = 0;

    for (int g = 0; g < GLYPH_COUNT; g++) {
        for (int id = 0; id < 7; id++) {
            CRGB buffer[GUARD];
            for (int i = 0; i < GUARD; i++) buffer[i] = sentinel;

            LedGlyph glyph(buffer, static_cast<GlyphId>(id));
            glyph.setGlyph(static_cast<Glyph>(g));
            glyph.setColor(CRGB(200, 100, 50));
            glyph.render(300);

            for (int i = 0; i < GUARD; i++) {
                if (buffer[i] == sentinel) continue;
                writes++;
                if (!allowed(id, i)) {
                    printf("FAIL glyph=%d id=%d wrote slot %d\n", g, id, i);
                    failures++;
                }
            }
        }
    }

    // Glyph 8 on digit A must light all 15 live slots of module A (58-73 minus dead 60).
    {
        CRGB buffer[GUARD];
        for (int i = 0; i < GUARD; i++) buffer[i] = sentinel;
        LedGlyph glyph(buffer, GlyphId::A);
        glyph.setGlyph(Glyph::D8);
        glyph.render(300);
        int lit = 0;
        for (int i = 58; i <= 73; i++) if (buffer[i] != sentinel) lit++;
        if (lit != 15) {
            printf("FAIL digit 8 on A lit %d slots, expected 15\n", lit);
            failures++;
        }
    }

    // Border and back indicators are independent LedDisplay concepts.
    // Border: top {2,3,7,8} = top colour, bottom {0,1,5,6} = bottom colour, regardless of
    // sameSideMode. Indicators (A=9, B=4) swap with sameSideMode.
    for (int sameSide = 0; sameSide < 2; sameSide++) {
        CRGB buffer[GUARD];
        const CRGB off(0, 0, 0);
        const CRGB a(10, 0, 0);
        const CRGB b(0, 20, 0);
        const CRGB other(0, 0, 30);
        const int top[] = {2, 3, 7, 8};
        const int bottom[] = {0, 1, 5, 6};
        const CRGB expect9 = sameSide ? b : a;   // indicator A's slot shows B when swapped
        const CRGB expect4 = sameSide ? a : b;

        LedDisplay display(buffer);
        auto renderAt = [&](const uint32_t ms) {
            for (int i = 0; i < GUARD; i++) buffer[i] = off;
            g_fakeMillis = ms;
            display.render();
        };
        display.setSameSideMode(sameSide == 1);
        display.setGlyphsGlyph(Glyph::Empty, Glyph::Empty, Glyph::Empty, Glyph::Empty);

        // Fresh display: border and indicators both off.
        renderAt(1300);   // visible blink phase
        for (int i = 0; i < 10; i++) {
            if (!(buffer[i] == off)) { printf("FAIL sameSide=%d default slot %d lit\n", sameSide, i); failures++; }
        }

        // Both on.
        display.setPlayersIndicatorsState(true);
        display.setIndicatorAppearancePlayerA(Color(10, 0, 0));
        display.setIndicatorAppearancePlayerB(Color(0, 20, 0));
        display.setBorderEnabled(true);
        display.setBorderAppearance(Color(10, 0, 0), Color(0, 20, 0));
        renderAt(1300);
        for (int k = 0; k < 4; k++) {
            if (!(buffer[top[k]] == a)) { printf("FAIL sameSide=%d top slot %d\n", sameSide, top[k]); failures++; }
            if (!(buffer[bottom[k]] == b)) { printf("FAIL sameSide=%d bottom slot %d\n", sameSide, bottom[k]); failures++; }
        }
        if (!(buffer[9] == expect9) || !(buffer[4] == expect4)) {
            printf("FAIL sameSide=%d indicators 4/9 wrong\n", sameSide);
            failures++;
        }
        for (int i = 10; i < GUARD; i++) {
            if (!(buffer[i] == off)) { printf("FAIL stray write slot %d\n", i); failures++; }
        }

        // Indicator calls never repaint the border.
        display.setIndicatorAppearancePlayerA(Color(0, 0, 30));
        display.setIndicatorAppearancePlayerB(Color(0, 0, 30));
        renderAt(1300);
        if (!(buffer[2] == a) || !(buffer[0] == b)) { printf("FAIL sameSide=%d indicator call repainted border\n", sameSide); failures++; }
        if (!(buffer[9] == other) || !(buffer[4] == other)) { printf("FAIL sameSide=%d indicators not recoloured\n", sameSide); failures++; }

        // Dark blink phase: top border half dark, bottom still lit.
        display.setBorderAppearance(Color(10, 0, 0), Color(0, 20, 0), true, false);
        renderAt(1100);
        if (!(buffer[2] == off) || !(buffer[0] == b)) { printf("FAIL sameSide=%d blink phase\n", sameSide); failures++; }

        // Border off, indicators on: only 4/9 lit.
        display.setBorderEnabled(false);
        renderAt(1300);
        for (int k = 0; k < 4; k++) {
            if (!(buffer[top[k]] == off) || !(buffer[bottom[k]] == off)) {
                printf("FAIL sameSide=%d border off but slot lit\n", sameSide);
                failures++;
            }
        }
        if (!(buffer[9] == other) || !(buffer[4] == other)) { printf("FAIL sameSide=%d indicators off with border\n", sameSide); failures++; }

        // Indicators off, border on: 4/9 dark, border lit.
        display.setPlayersIndicatorsState(false);
        display.setBorderEnabled(true);
        renderAt(1300);
        if (!(buffer[9] == off) || !(buffer[4] == off)) { printf("FAIL sameSide=%d indicators lit while off\n", sameSide); failures++; }
        if (!(buffer[2] == a) || !(buffer[0] == b)) { printf("FAIL sameSide=%d border off with indicators\n", sameSide); failures++; }

        // Both off: whole centre block dark.
        display.setBorderEnabled(false);
        renderAt(1300);
        for (int i = 0; i < 10; i++) {
            if (!(buffer[i] == off)) { printf("FAIL sameSide=%d disabled slot %d lit\n", sameSide, i); failures++; }
        }
    }

    // Boot sweep + celebration (LedSweepAnimation), shared with check_v1.cpp.
    const SweepCheckConfig v2Sweep{
        "v2", 74, v2SweepReserved,
        /* indicatorA, indicatorB */ 9, 4,
        /* near (border)          */ 0, 9,
        /* far A (digit A)        */ 58, 73,
        /* far B (digit D)        */ 10, 25,
        /* digit proof range      */ 10, 73,
    };
    failures += runSweepChecks(v2Sweep, writes);

    // Per-segment brightness compensation (task #43): recompute each factor
    // independently from the measured areas/dies and compare against
    // NineSegmentBrightness, check indicators/dead slots stay untouched, and
    // bound the worst-case die-equivalent draw against the old flat-0.8 limit.
    {
        struct SegmentFact {
            const char *name;
            int moduleSlots[3];
            int slotCount;
            int dies;
            float totalMm2;
        };
        static const SegmentFact segments[] = {
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
        const float TOLERANCE = 1.2f;
        const float REFERENCE = 383.0f;   // bottom-right's mm^2/die - the largest measured

        double perModuleDieEquivalents = 0.0;

        for (const auto &seg : segments) {
            const float mm2PerDie = seg.totalMm2 / static_cast<float>(seg.dies);
            const float ratio = TOLERANCE * mm2PerDie / REFERENCE;
            const int expected = ratio >= 1.0f ? 255 : static_cast<int>(std::lround(ratio * 255.0f));

            for (int i = 0; i < seg.slotCount; i++) {
                const auto moduleSlot = static_cast<uint16_t>(seg.moduleSlots[i]);
                const int actual = NineSegmentBrightness::moduleFactor(moduleSlot);
                if (actual != expected) {
                    printf("FAIL compensation segment=%s moduleSlot=%d factor=%d expected=%d\n",
                           seg.name, moduleSlot, actual, expected);
                    failures++;
                }
            }

            perModuleDieEquivalents += seg.dies * (expected / 255.0);
        }

        // Border: same formula, 473 mm^2 across 2 dies per segment, no special case.
        const float borderMm2PerDie = 473.0f / 2.0f;
        const float borderRatio = TOLERANCE * borderMm2PerDie / REFERENCE;
        const int expectedBorder = borderRatio >= 1.0f ? 255 : static_cast<int>(std::lround(borderRatio * 255.0f));
        const int borderSlots[] = {0, 1, 2, 3, 5, 6, 7, 8};
        for (const int slot : borderSlots) {
            const int actual = NineSegmentBrightness::slotScale(static_cast<uint16_t>(slot));
            if (actual != expectedBorder) {
                printf("FAIL compensation border slot=%d factor=%d expected=%d\n", slot, actual, expectedBorder);
                failures++;
            }
        }
        const double borderDieEquivalents = 8 * (expectedBorder / 255.0);   // 8 border slots, 1 die each

        // Back indicators (4, 9) and every module's dead chain position: never
        // front-facing / never lit, must stay at 255 (identity, untouched).
        const int untouched[] = {4, 9, 12, 28, 44, 60};
        for (const int slot : untouched) {
            const int actual = NineSegmentBrightness::slotScale(static_cast<uint16_t>(slot));
            if (actual != 255) {
                printf("FAIL compensation slot=%d expected untouched (255), got %d\n", slot, actual);
                failures++;
            }
        }
        const double indicatorDieEquivalents = 2 * 1.0;   // both indicators, always full

        // Worst case: four "8" glyphs (every segment lit) + border + indicators, at
        // brightness level 8, must not exceed the old flat-0.8 bound (72 = 0.8 x 90
        // dies) - compensation replaces that power guard on V2.
        const double worstCase = 4 * perModuleDieEquivalents + borderDieEquivalents + indicatorDieEquivalents;
        if (worstCase > 72.0) {
            printf("FAIL compensation worst-case %.2f die-equivalents exceeds the 72 bound\n", worstCase);
            failures++;
        }
        printf("compensation: worst-case %.2f die-equivalents (bound 72)\n", worstCase);

        // compensate() applied to an all-white buffer must reproduce scale8(slotScale(i))
        // per slot, everywhere in the chain - not just at the sampled slots above.
        CRGB buffer[Board::LED_COUNT];
        for (int i = 0; i < Board::LED_COUNT; i++) buffer[i] = CRGB(255, 255, 255);
        NineSegmentBrightness::compensate(buffer);
        for (int i = 0; i < Board::LED_COUNT; i++) {
            const uint8_t factor = NineSegmentBrightness::slotScale(static_cast<uint16_t>(i));
            const CRGB expected = CRGB(255, 255, 255).scale8(factor);
            if (!(buffer[i] == expected)) {
                printf("FAIL compensate() slot=%d got (%d,%d,%d) expected (%d,%d,%d)\n",
                       i, buffer[i].r, buffer[i].g, buffer[i].b, expected.r, expected.g, expected.b);
                failures++;
            }
        }
    }

    printf("%s: %d writes checked, %d failures\n", failures ? "FAIL" : "OK", writes, failures);
    return failures ? 1 : 0;
}
