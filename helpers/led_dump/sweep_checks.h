// Board-agnostic LedSweepAnimation invariants (boot sweep + layered celebration),
// parameterised by a small per-board descriptor so check_v2.cpp and check_v1.cpp
// share one implementation. Task #52 turned the celebration from a front takeover
// into layer 2 over the GameOver screen, so these checks now prove the base shows
// through: every slot off the ring equals a celebration-free reference frame.
#ifndef LED_DUMP_SWEEP_CHECKS_H
#define LED_DUMP_SWEEP_CHECKS_H

#include <cstdio>

#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Animation/LedSweepAnimation.h"

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
};

static bool sweepCheckSwept(const SweepCheckConfig &cfg, const int slot) {
    return slot >= 0 && slot < cfg.ledCount && !cfg.isReserved(slot);
}

static void sweepCheckSetUp(LedDisplay &display) {
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

static int runSweepChecks(const SweepCheckConfig &cfg, int &writes) {
    int failures = 0;
    const CRGB off(0, 0, 0);

    // Boot sweep through LedDisplay::renderBootFrame: front slots only, no blank
    // frame, ends dark, reaches both digit corners and the near ring.
    {
        const uint32_t duration = LedSweepAnimation::bootParams().durationMs;
        bool litNear = false;
        bool litFarA = false;
        bool litFarB = false;

        CRGB buffer[Board::LED_COUNT];
        LedDisplay display(buffer);

        for (uint32_t t = 0; t <= duration + 50; t += 5) {
            display.renderBootFrame(t);

            int lit = 0;
            for (int i = 0; i < Board::LED_COUNT; i++) {
                if (buffer[i] == off) continue;

                if (!sweepCheckSwept(cfg, i)) {
                    printf("FAIL %s sweep t=%u lit slot %d\n", cfg.boardName, t, i);
                    failures++;
                    continue;
                }

                writes++;
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

    // Celebration as layer 2, for every blend the demo offers and both sides (the
    // origin sits on the winner's half, which moves every slot's radius and so the
    // radial gaps the ring has to clear). The ring is taken from a standalone
    // sweep with the same params, so each slot is either off the ring (must equal
    // the celebration-free reference exactly) or on it (must equal the blend of
    // the reference with the ring colour).
    const BlendMode modes[] = {BlendMode::Normal, BlendMode::Screen, BlendMode::Add, BlendMode::Lighten};
    const CRGB red(255, 0, 0);
    const Color win(0, 255, 0);
    const LedSweepAnimation::Params p = LedSweepAnimation::celebrationParams();
    const uint32_t cycle = static_cast<uint32_t>(p.durationMs) + p.gapMs;
    const uint32_t total = cycle * p.repeats - p.gapMs;
    const uint32_t start = 5000;

    for (const BlendMode mode : modes) {
        for (int leftWon = 0; leftWon <= 1; leftWon++) {
            CRGB buffer[Board::LED_COUNT];
            CRGB reference[Board::LED_COUNT];
            CRGB ring[Board::LED_COUNT];
            LedDisplay display(buffer);
            LedDisplay plain(reference);
            sweepCheckSetUp(display);
            sweepCheckSetUp(plain);

            LedSweepAnimation::Params ringParams = p;
            ringParams.solid = CRGB(0, 255, 0);
            LedSweepAnimation ringSweep(ringParams);
            ringSweep.setOriginToHalf(leftWon == 1);
            ringSweep.start(start);

            auto renderAt = [&](const uint32_t ms) {
                for (int i = 0; i < Board::LED_COUNT; i++) {
                    buffer[i] = off;
                    reference[i] = off;
                    ring[i] = off;
                }
                g_fakeMillis = ms;
                display.render();
                plain.render();
                ringSweep.render(ms, ring);
            };
            auto matchesReference = [&]() {
                for (int i = 0; i < Board::LED_COUNT; i++) {
                    if (!(buffer[i] == reference[i])) return false;
                }
                return true;
            };

            display.setCelebrationBlend(mode);
            g_fakeMillis = start;
            display.startCelebration(win, leftWon == 1);

            for (uint32_t t = 0; t < total; t += 10) {
                renderAt(start + t);

                const bool inGap = t % cycle >= p.durationMs;
                int lit = 0;

                for (int i = 0; i < Board::LED_COUNT; i++) {
                    writes++;

                    if (!(ring[i] == off)) {
                        lit++;
                        if (!sweepCheckSwept(cfg, i) || inGap) {
                            printf("FAIL %s %s leftWon=%d t=%u ring on slot %d (gap=%d)\n",
                                   cfg.boardName, blendName(mode), leftWon, t, i, inGap);
                            failures++;
                        }
                        if (ring[i].r != 0 || ring[i].b != 0 || ring[i].g == 0) {
                            printf("FAIL %s ring slot %d not solid green\n", cfg.boardName, i);
                            failures++;
                        }
                    }

                    const CRGB expected = blendPixel(reference[i], ring[i], mode);
                    if (!(buffer[i] == expected)) {
                        printf("FAIL %s %s leftWon=%d t=%u slot %d (%d,%d,%d) expected (%d,%d,%d)\n",
                               cfg.boardName, blendName(mode), leftWon, t, i,
                               buffer[i].r, buffer[i].g, buffer[i].b, expected.r, expected.g, expected.b);
                        failures++;
                        continue;
                    }

                    if (!(ring[i] == off) && buffer[i].g == 0) {
                        printf("FAIL %s %s slot %d ring invisible\n", cfg.boardName, blendName(mode), i);
                        failures++;
                    }
                    if (mode != BlendMode::Normal
                        && (buffer[i].r < reference[i].r || buffer[i].g < reference[i].g || buffer[i].b < reference[i].b)) {
                        printf("FAIL %s %s slot %d darker than the base\n", cfg.boardName, blendName(mode), i);
                        failures++;
                    }
                }

                // Indicators face the players and stay exactly as the base drew them.
                if (!(buffer[cfg.indicatorA] == red) || !(buffer[cfg.indicatorB] == red)) {
                    printf("FAIL %s %s leftWon=%d t=%u indicators not red\n", cfg.boardName, blendName(mode), leftWon, t);
                    failures++;
                }

                if (lit == 0 && !inGap) {
                    printf("FAIL %s %s leftWon=%d t=%u ring lit nothing\n", cfg.boardName, blendName(mode), leftWon, t);
                    failures++;
                }
            }

            // Past the last cycle the frame is the plain screen, byte for byte.
            renderAt(start + total);
            if (!matchesReference()) {
                printf("FAIL %s %s leftWon=%d frame differs from the base after %u ms\n",
                       cfg.boardName, blendName(mode), leftWon, total);
                failures++;
            }

            // resetAnimations() mid-cycle ends it on the very next frame. It also
            // clears V1's bar, so the reference gets the same call.
            g_fakeMillis = start;
            display.startCelebration(win, leftWon == 1);
            renderAt(start + 500);
            display.resetAnimations();
            plain.resetAnimations();
            renderAt(start + 550);
            if (!matchesReference()) {
                printf("FAIL %s %s leftWon=%d celebration survived resetAnimations()\n",
                       cfg.boardName, blendName(mode), leftWon);
                failures++;
            }
        }
    }

    return failures;
}

#endif //LED_DUMP_SWEEP_CHECKS_H
