// Board-agnostic LedSweepAnimation invariants (boot sweep + celebration takeover),
// parameterised by a small per-board descriptor so check_v2.cpp and check_v1.cpp
// share one implementation. Extracted from check_v2.cpp (task #45); its own output
// is unchanged by the extraction.
#ifndef LED_DUMP_SWEEP_CHECKS_H
#define LED_DUMP_SWEEP_CHECKS_H

#include <cstdio>

#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Animation/LedSweepAnimation.h"

// Oversized so an out-of-range write is caught rather than corrupting the stack.
static const int SWEEP_CHECK_GUARD = 256;

struct SweepCheckConfig {
    const char *boardName;
    uint16_t ledCount;

    // Slots the sweep must never touch: back indicators plus (V2 only) dead chain
    // positions. Swept slots are everything else below ledCount.
    bool (*isReserved)(int slot);
    int indicatorA;   // slot lit red for player A throughout the celebration
    int indicatorB;   // slot lit red for player B throughout the celebration

    // Boot-sweep reach probes: proves the ring actually gets from the origin
    // (near) out to both far corners, not just some middle radius.
    int nearLo, nearHi;
    int farALo, farAHi;
    int farBLo, farBHi;

    // Post-celebration / post-reset proof: a range that shows the caller's blue
    // digit colour again once the takeover ends (digits only - excludes the
    // border/indicator block and, on V1, the colon and history bar).
    int digitProofLo, digitProofHi;
};

static bool sweepCheckSwept(const SweepCheckConfig &cfg, const int slot) {
    return slot >= 0 && slot < cfg.ledCount && !cfg.isReserved(slot);
}

static int runSweepChecks(const SweepCheckConfig &cfg, int &writes) {
    int failures = 0;
    const CRGB sentinel(1, 2, 3);
    const CRGB off(0, 0, 0);

    // Boot sweep: front slots only, no blank frame, ends dark, reaches both
    // digit corners and the near ring.
    {
        const LedSweepAnimation::Params boot = LedSweepAnimation::bootParams();
        const uint32_t duration = boot.durationMs;
        bool litNear = false;
        bool litFarA = false;
        bool litFarB = false;

        for (uint32_t t = 0; t <= duration + 50; t += 5) {
            CRGB buffer[SWEEP_CHECK_GUARD];
            for (int i = 0; i < SWEEP_CHECK_GUARD; i++) buffer[i] = sentinel;

            LedSweepAnimation(buffer, boot).renderFrame(t);

            int lit = 0;
            for (int i = 0; i < SWEEP_CHECK_GUARD; i++) {
                if (buffer[i] == sentinel) continue;

                if (!sweepCheckSwept(cfg, i)) {
                    printf("FAIL %s sweep t=%u wrote slot %d\n", cfg.boardName, t, i);
                    failures++;
                    continue;
                }

                writes++;
                if (buffer[i] == off) continue;

                lit++;
                if (i >= cfg.nearLo && i <= cfg.nearHi) litNear = true;
                if (i >= cfg.farALo && i <= cfg.farAHi) litFarA = true;
                if (i >= cfg.farBLo && i <= cfg.farBHi) litFarB = true;

                if (t >= duration) {
                    printf("FAIL %s sweep t=%u slot %d still lit after the sweep\n", cfg.boardName, t, i);
                    failures++;
                }
            }

            // BAND is sized so the ring always covers at least one LED - the field has
            // radial gaps (empty centre, gaps between clusters) that a thinner band
            // would fall into, leaving the strip visibly blank mid-animation.
            if (lit == 0 && t < duration) {
                printf("FAIL %s sweep t=%u lit nothing\n", cfg.boardName, t);
                failures++;
            }
        }

        if (!litNear || !litFarA || !litFarB) {
            printf("FAIL %s sweep never reached near=%d farA=%d farB=%d\n",
                   cfg.boardName, litNear, litFarA, litFarB);
            failures++;
        }
    }

    // Celebration, driven through LedDisplay::render(): the sweep paints pure
    // green, the glyphs and border pure blue, the indicators pure red - so a blue
    // channel on any swept slot means the glyph layer leaked through the
    // takeover, and a missing red on the indicators means they were wrongly
    // suppressed. Both sides are swept: the origin sits on the winner's half,
    // which changes every slot's radius and so the radial gaps the ring has to
    // clear.
    for (int leftWon = 0; leftWon <= 1; leftWon++) {
        const CRGB red(255, 0, 0);
        const Color win(0, 255, 0);

        CRGB buffer[SWEEP_CHECK_GUARD];
        LedDisplay display(buffer);

        auto renderAt = [&](const uint32_t ms) {
            for (int i = 0; i < SWEEP_CHECK_GUARD; i++) buffer[i] = sentinel;
            g_fakeMillis = ms;
            display.render();
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

            for (int i = 0; i < SWEEP_CHECK_GUARD; i++) {
                if (sweepCheckSwept(cfg, i)) {
                    if (buffer[i] == sentinel) {
                        printf("FAIL %s celebration leftWon=%d t=%u slot %d never written\n",
                               cfg.boardName, leftWon, t, i);
                        failures++;
                        continue;
                    }

                    writes++;

                    // Sweep output is the solid colour scaled: green only, never r or b.
                    if (buffer[i].r != 0 || buffer[i].b != 0) {
                        printf("FAIL %s celebration leftWon=%d t=%u slot %d not sweep-only (%d,%d,%d)\n",
                               cfg.boardName, leftWon, t, i, buffer[i].r, buffer[i].g, buffer[i].b);
                        failures++;
                    }

                    if (!(buffer[i] == off)) {
                        lit++;
                        if (inGap) {
                            printf("FAIL %s celebration leftWon=%d t=%u slot %d lit during the gap\n",
                                   cfg.boardName, leftWon, t, i);
                            failures++;
                        }
                    }
                    continue;
                }

                // Indicators face the players and must survive the takeover.
                if (i == cfg.indicatorA || i == cfg.indicatorB) {
                    if (!(buffer[i] == red)) {
                        printf("FAIL %s celebration leftWon=%d t=%u indicator slot %d not lit\n",
                               cfg.boardName, leftWon, t, i);
                        failures++;
                    }
                    continue;
                }

                // Dead slots and anything past the chain: nobody may touch them.
                if (!(buffer[i] == sentinel)) {
                    printf("FAIL %s celebration leftWon=%d t=%u wrote reserved slot %d\n",
                           cfg.boardName, leftWon, t, i);
                    failures++;
                }
            }

            if (lit == 0 && !inGap) {
                printf("FAIL %s celebration leftWon=%d t=%u lit nothing\n", cfg.boardName, leftWon, t);
                failures++;
            }
        }

        // Past the last cycle the normal screen is back: the blue digits render again.
        renderAt(base + total);
        bool blueDigit = false;
        for (int i = cfg.digitProofLo; i <= cfg.digitProofHi; i++) {
            if (buffer[i] != sentinel && buffer[i].b != 0) blueDigit = true;
        }
        if (!blueDigit) {
            printf("FAIL %s celebration leftWon=%d did not hand the screen back after %u ms\n",
                   cfg.boardName, leftWon, total);
            failures++;
        }

        // resetAnimations() mid-cycle ends it on the very next frame.
        g_fakeMillis = base;
        display.startCelebration(win, leftWon == 1);
        renderAt(base + 500);
        display.resetAnimations();
        renderAt(base + 550);
        blueDigit = false;
        for (int i = cfg.digitProofLo; i <= cfg.digitProofHi; i++) {
            if (buffer[i] != sentinel && buffer[i].b != 0) blueDigit = true;
        }
        if (!blueDigit) {
            printf("FAIL %s celebration leftWon=%d survived resetAnimations()\n", cfg.boardName, leftWon);
            failures++;
        }
    }

    return failures;
}

#endif //LED_DUMP_SWEEP_CHECKS_H
