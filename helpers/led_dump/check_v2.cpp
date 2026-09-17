// V2 (BOARD_REV=2) invariants for the glyph layer, checked on the host:
//   - every write is inside the 74-slot chain
//   - dead slots 12/28/44/60 are never written
//   - digits stay inside their own module (A=58-73, B=42-57, C=26-41, D=10-25)
//     and never touch the centre block (border 0-3/5-8, indicators 4/9)
//   - indicator A writes only slot 9, B only slot 4; the colon writes nothing
//   - border and back indicators are enabled/coloured independently
//   - the boot sweep stays on front slots, ends dark, and never blanks a frame
//   - the celebration takes over the front: digits/colon/border sit out, indicators do not
// Build: ./check_v2.sh
#include <cstdio>

#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Animation/LedSweepAnimation.h"

uint32_t g_fakeMillis = 0;
CFastLED FastLED;

static const int GUARD = 256;   // oversized so out-of-range writes are caught

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

    // Boot sweep (LedSweepAnimation): front slots only, no blank frame, ends dark.
    {
        const CRGB off(0, 0, 0);
        const LedSweepAnimation::Params boot = LedSweepAnimation::bootParams();
        const uint32_t duration = boot.durationMs;
        bool litBorder = false;
        bool litDigitA = false;
        bool litDigitD = false;

        for (uint32_t t = 0; t <= duration + 50; t += 5) {
            CRGB buffer[GUARD];
            for (int i = 0; i < GUARD; i++) buffer[i] = sentinel;

            LedSweepAnimation(buffer, boot).renderFrame(t);

            int lit = 0;
            for (int i = 0; i < GUARD; i++) {
                if (buffer[i] == sentinel) continue;

                // Back indicators, dead slots and anything past the chain must stay untouched.
                if (i >= 74 || i == 4 || i == 9 || i == 12 || i == 28 || i == 44 || i == 60) {
                    printf("FAIL sweep t=%u wrote slot %d\n", t, i);
                    failures++;
                    continue;
                }

                writes++;
                if (buffer[i] == off) continue;

                lit++;
                if (i < 10) litBorder = true;
                if (i >= 58 && i <= 73) litDigitA = true;
                if (i >= 10 && i <= 25) litDigitD = true;

                if (t >= duration) {
                    printf("FAIL sweep t=%u slot %d still lit after the sweep\n", t, i);
                    failures++;
                }
            }

            // BAND is sized so the ring always covers at least one LED - the field has
            // radial gaps (empty centre, gaps between border and digits) that a thinner
            // band would fall into, leaving the strip visibly blank mid-animation.
            if (lit == 0 && t < duration) {
                printf("FAIL sweep t=%u lit nothing\n", t);
                failures++;
            }
        }

        if (!litBorder || !litDigitA || !litDigitD) {
            printf("FAIL sweep never reached border=%d digitA=%d digitD=%d\n",
                   litBorder, litDigitA, litDigitD);
            failures++;
        }
    }

    // Celebration, driven through LedDisplay::render() rather than the animation alone:
    // the sweep paints pure green, the glyphs and border pure blue, the indicators pure
    // red - so a blue channel on any swept slot means the glyph layer leaked through the
    // takeover, and a missing red on 4/9 means the indicators were wrongly suppressed.
    // Both sides are swept: the origin sits on the winner's half, which changes every
    // slot's radius and so the radial gaps the ring has to clear.
    for (int leftWon = 0; leftWon <= 1; leftWon++) {
        const CRGB off(0, 0, 0);
        const CRGB red(255, 0, 0);
        const Color win(0, 255, 0);

        CRGB buffer[GUARD];
        LedDisplay display(buffer);

        auto renderAt = [&](const uint32_t ms) {
            for (int i = 0; i < GUARD; i++) buffer[i] = sentinel;
            g_fakeMillis = ms;
            display.render();
        };

        auto swept = [](const int slot) {
            return slot < 74 && slot != 4 && slot != 9
                   && slot != 12 && slot != 28 && slot != 44 && slot != 60;
        };

        display.setSameSideMode(false);
        display.setNumericValue(11, 9);
        display.setGlyphsAppearance(Colors::Blue, Colors::Blue);
        display.setBorderEnabled(true);
        display.setBorderAppearance(Colors::Blue, Colors::Blue);
        display.setPlayersIndicatorsState(true);
        display.setIndicatorAppearancePlayerA(Colors::Red);
        display.setIndicatorAppearancePlayerB(Colors::Red);

        const LedSweepAnimation::Params p = LedSweepAnimation::celebrationParams();
        const uint32_t cycle = static_cast<uint32_t>(p.durationMs) + p.gapMs;
        const uint32_t total = cycle * p.repeats - p.gapMs;
        const uint32_t base = 5000;

        g_fakeMillis = base;
        display.startCelebration(win, leftWon == 1);

        for (uint32_t t = 0; t < total; t += 50) {
            renderAt(base + t);

            const uint32_t within = t % cycle;
            const bool inGap = within >= p.durationMs;
            int lit = 0;

            for (int i = 0; i < GUARD; i++) {
                if (swept(i)) {
                    if (buffer[i] == sentinel) {
                        printf("FAIL celebration leftWon=%d t=%u slot %d never written\n",
                               leftWon, t, i);
                        failures++;
                        continue;
                    }

                    writes++;

                    // Sweep output is the solid colour scaled: green only, never r or b.
                    if (buffer[i].r != 0 || buffer[i].b != 0) {
                        printf("FAIL celebration leftWon=%d t=%u slot %d not sweep-only (%d,%d,%d)\n",
                               leftWon, t, i, buffer[i].r, buffer[i].g, buffer[i].b);
                        failures++;
                    }

                    if (!(buffer[i] == off)) {
                        lit++;
                        if (inGap) {
                            printf("FAIL celebration leftWon=%d t=%u slot %d lit during the gap\n",
                                   leftWon, t, i);
                            failures++;
                        }
                    }
                    continue;
                }

                // Indicators face the players and must survive the takeover.
                if (i == 4 || i == 9) {
                    if (!(buffer[i] == red)) {
                        printf("FAIL celebration leftWon=%d t=%u indicator slot %d not lit\n",
                               leftWon, t, i);
                        failures++;
                    }
                    continue;
                }

                // Dead slots and anything past the chain: nobody may touch them.
                if (!(buffer[i] == sentinel)) {
                    printf("FAIL celebration leftWon=%d t=%u wrote reserved slot %d\n",
                           leftWon, t, i);
                    failures++;
                }
            }

            if (lit == 0 && !inGap) {
                printf("FAIL celebration leftWon=%d t=%u lit nothing\n", leftWon, t);
                failures++;
            }
        }

        // Past the last cycle the normal screen is back: the blue digits render again.
        renderAt(base + total);
        bool blueDigit = false;
        for (int i = 10; i < 74; i++) {
            if (buffer[i] != sentinel && buffer[i].b != 0) blueDigit = true;
        }
        if (!blueDigit) {
            printf("FAIL celebration leftWon=%d did not hand the screen back after %u ms\n",
                   leftWon, total);
            failures++;
        }

        // resetAnimations() mid-cycle ends it on the very next frame.
        g_fakeMillis = base;
        display.startCelebration(win, leftWon == 1);
        renderAt(base + 500);
        display.resetAnimations();
        renderAt(base + 550);
        blueDigit = false;
        for (int i = 10; i < 74; i++) {
            if (buffer[i] != sentinel && buffer[i].b != 0) blueDigit = true;
        }
        if (!blueDigit) {
            printf("FAIL celebration leftWon=%d survived resetAnimations()\n", leftWon);
            failures++;
        }
    }

    printf("%s: %d writes checked, %d failures\n", failures ? "FAIL" : "OK", writes, failures);
    return failures ? 1 : 0;
}
