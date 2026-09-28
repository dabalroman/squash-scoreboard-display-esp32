// Board-agnostic LedIntroAnimation invariants (task #56, the walk-on wipe before
// every game), shared by check_v1.cpp and check_v2.cpp. The wipe is a fill, not
// a ring: it starts at the outer edge, never un-lights a slot, and from the end
// of the wipe through the hold paints every live front slot in its half's colour
// (the x == 0 seam, V1's colon, stays dark).
#ifndef LED_DUMP_INTRO_CHECKS_H
#define LED_DUMP_INTRO_CHECKS_H

#include <cmath>
#include <cstdio>

#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Animation/LedIntroAnimation.h"

struct IntroCheckConfig {
    const char *boardName;
    bool (*isReserved)(int slot);   // SKIP slots: back indicators, V2's dead chain positions
    int indicatorA;
    int indicatorB;
    int seamSlots;                  // slots at x == 0: V1's two colon dies, none on V2
    uint32_t expectedTotalMs;       // wipe+hold(+out, only where something is outgoing)
};

static float introDistance(const int slot) {
    const float x = static_cast<float>(LedSlots::POS[slot][0]);
    const float y = static_cast<float>(LedSlots::POS[slot][1]);
    return std::sqrt(x * x + y * y);
}

static int runIntroChecks(const IntroCheckConfig &cfg) {
    int failures = 0;
    const CRGB off(0, 0, 0);
    const CRGB sentinel(1, 2, 3);
    const CRGB leftColor(255, 0, 40);
    const CRGB rightColor(0, 90, 255);
    const LedIntroAnimation::Params p = LedIntroAnimation::defaults();

    // Independent reference: farthest live slot, and the seam count.
    float maxRadius = 0.0f;
    int seams = 0;
    for (int i = 0; i < Board::LED_COUNT; i++) {
        const bool skip = LedSlots::POS[i][0] == LedSlots::SKIP;
        if (skip != cfg.isReserved(i)) {
            printf("FAIL %s intro slot %d SKIP=%d disagrees with the reserved list\n", cfg.boardName, i, skip);
            failures++;
        }
        if (skip) continue;
        if (LedSlots::POS[i][0] == 0) seams++;
        if (introDistance(i) > maxRadius) maxRadius = introDistance(i);
    }
    if (seams != cfg.seamSlots) {
        printf("FAIL %s intro %d seam slots, expected %d\n", cfg.boardName, seams, cfg.seamSlots);
        failures++;
    }

    // Real elementMap (never a hand-kept slot list here): outgoing = LedTarget::Bar
    // (empty on V2), colour override = BorderTop -> left / BorderBottom -> right
    // (empty on V1 - LedCentralScreenBorder's markSlots is a no-op there).
    CRGB elementMapScratch[Board::LED_COUNT];
    LedDisplay elementMapSource(elementMapScratch);
    uint16_t elementMap[Board::LED_COUNT];
    for (int i = 0; i < Board::LED_COUNT; i++) elementMap[i] = elementMapSource.elementsAt(i);

    auto introIsOutgoing = [&](const int i) { return (elementMap[i] & LedTarget::Bar) != 0; };
    auto introSideColor = [&](const int i) -> CRGB {
        if (elementMap[i] & LedTarget::BorderTop) return leftColor;
        if (elementMap[i] & LedTarget::BorderBottom) return rightColor;
        return LedSlots::POS[i][0] < 0 ? leftColor : rightColor;
    };

    // Standalone animation, stepped clock-free.
    {
        LedIntroAnimation intro;
        intro.start(0, leftColor, rightColor, elementMap, LedTarget::Bar, LedTarget::BorderTop, LedTarget::BorderBottom);
        const uint32_t total = intro.totalMs();
        const uint32_t holdEnd = static_cast<uint32_t>(p.wipeMs) + p.holdMs;

        if (total != cfg.expectedTotalMs) {
            printf("FAIL %s intro totalMs=%u expected %u\n", cfg.boardName, total, cfg.expectedTotalMs);
            failures++;
        }

        CRGB holdFrame[Board::LED_COUNT];
        intro.renderFrame(holdEnd, holdFrame);

        CRGB prev[Board::LED_COUNT];
        for (int i = 0; i < Board::LED_COUNT; i++) prev[i] = sentinel;
        bool firstLitSeen = false;

        for (uint32_t t = 0; t <= total; t += 10) {
            CRGB out[Board::LED_COUNT];
            for (int i = 0; i < Board::LED_COUNT; i++) out[i] = sentinel;
            intro.renderFrame(t, out);

            bool anyLit = false;
            for (int i = 0; i < Board::LED_COUNT; i++) {
                const bool skip = LedSlots::POS[i][0] == LedSlots::SKIP;
                const bool seam = !skip && LedSlots::POS[i][0] == 0;
                const bool written = !(out[i] == sentinel);

                if ((skip || seam) && written) {
                    printf("FAIL %s intro t=%u wrote %s slot %d\n", cfg.boardName, t, skip ? "SKIP" : "seam", i);
                    failures++;
                    continue;
                }
                if (!written) {
                    if (!(prev[i] == sentinel)) {
                        printf("FAIL %s intro t=%u slot %d went dark again\n", cfg.boardName, t, i);
                        failures++;
                    }
                    continue;
                }

                anyLit = true;
                const CRGB side = introSideColor(i);

                // Monotone lit-and-held applies only through wipe+hold; the out
                // phase fades outgoing slots back down on purpose.
                if (t <= holdEnd
                    && !(prev[i] == sentinel)
                    && (out[i].r < prev[i].r || out[i].g < prev[i].g || out[i].b < prev[i].b)) {
                    printf("FAIL %s intro t=%u slot %d dimmed\n", cfg.boardName, t, i);
                    failures++;
                }
                if (out[i].r > side.r || out[i].g > side.g || out[i].b > side.b) {
                    printf("FAIL %s intro t=%u slot %d brighter than its side's colour\n", cfg.boardName, t, i);
                    failures++;
                }
                if (t >= p.wipeMs && t <= holdEnd && !(out[i] == side)) {
                    printf("FAIL %s intro t=%u slot %d (%d,%d,%d) not its side's colour\n",
                           cfg.boardName, t, i, out[i].r, out[i].g, out[i].b);
                    failures++;
                }

                // Out phase: every other slot stays exactly as painted in the hold;
                // outgoing slots may only fade towards black.
                if (t > holdEnd) {
                    if (introIsOutgoing(i)) {
                        if (out[i].r > prev[i].r || out[i].g > prev[i].g || out[i].b > prev[i].b) {
                            printf("FAIL %s intro t=%u outgoing slot %d brightened in the out phase\n",
                                   cfg.boardName, t, i);
                            failures++;
                        }
                    } else if (!(out[i] == holdFrame[i])) {
                        printf("FAIL %s intro t=%u slot %d changed during the out phase\n", cfg.boardName, t, i);
                        failures++;
                    }
                }

                prev[i] = out[i];
            }

            // Outer edge first: the first frame lights only the outermost ring.
            if (anyLit && !firstLitSeen) {
                firstLitSeen = true;
                for (int i = 0; i < Board::LED_COUNT; i++) {
                    if (out[i] == sentinel) continue;
                    if (introDistance(i) < maxRadius - p.edge) {
                        printf("FAIL %s intro t=%u first lit slot %d at %.0f, not the outer edge (max %.0f)\n",
                               cfg.boardName, t, i, introDistance(i), maxRadius);
                        failures++;
                    }
                }
            }

            // From the end of the wipe through the hold, every live, non-seam slot
            // is painted (BorderTop/BorderBottom included, in their override colour).
            if (t >= p.wipeMs && t <= holdEnd) {
                for (int i = 0; i < Board::LED_COUNT; i++) {
                    if (LedSlots::POS[i][0] == LedSlots::SKIP || LedSlots::POS[i][0] == 0) continue;
                    if (out[i] == sentinel) {
                        printf("FAIL %s intro t=%u slot %d unpainted after the wipe\n", cfg.boardName, t, i);
                        failures++;
                    }
                }
            }
        }

        if (!firstLitSeen) {
            printf("FAIL %s intro never lit anything\n", cfg.boardName);
            failures++;
        }
        if (!intro.active(total - 1) || intro.active(total)) {
            printf("FAIL %s intro active() does not end at wipe+hold+out\n", cfg.boardName);
            failures++;
        }

        // Out phase: every outgoing slot must reach exactly black by the end, and
        // the nearest-to-centre ones must get there no later than farther ones.
        CRGB finalFrame[Board::LED_COUNT];
        intro.renderFrame(total, finalFrame);

        int firstDarkAt[Board::LED_COUNT];
        for (int i = 0; i < Board::LED_COUNT; i++) firstDarkAt[i] = -1;
        for (uint32_t t = holdEnd; t <= total; t += 10) {
            CRGB out[Board::LED_COUNT];
            intro.renderFrame(t, out);
            for (int i = 0; i < Board::LED_COUNT; i++) {
                if (introIsOutgoing(i) && firstDarkAt[i] < 0 && out[i] == off) {
                    firstDarkAt[i] = static_cast<int>(t);
                }
            }
        }

        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (!introIsOutgoing(i)) continue;
            if (!(finalFrame[i] == off)) {
                printf("FAIL %s intro outgoing slot %d not dark at the end\n", cfg.boardName, i);
                failures++;
            }
            if (firstDarkAt[i] < 0) {
                printf("FAIL %s intro outgoing slot %d never went dark\n", cfg.boardName, i);
                failures++;
            }
        }
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (!introIsOutgoing(i) || firstDarkAt[i] < 0) continue;
            for (int j = 0; j < Board::LED_COUNT; j++) {
                if (!introIsOutgoing(j) || firstDarkAt[j] < 0) continue;
                if (introDistance(i) < introDistance(j) && firstDarkAt[i] > firstDarkAt[j]) {
                    printf("FAIL %s intro outgoing slot %d (closer, r=%.0f) went dark after slot %d (farther, r=%.0f)\n",
                           cfg.boardName, i, introDistance(i), j, introDistance(j));
                    failures++;
                }
            }
        }

        // Visibly animated, not a pop at the end: halfway through the out phase the
        // bar is part gone, part still lit.
        if (total > holdEnd) {
            CRGB mid[Board::LED_COUNT];
            intro.renderFrame(holdEnd + (total - holdEnd) / 2, mid);
            int dark = 0, lit = 0;
            for (int i = 0; i < Board::LED_COUNT; i++) {
                if (!introIsOutgoing(i)) continue;
                if (mid[i] == off) dark++;
                else lit++;
            }
            if (dark == 0 || lit == 0) {
                printf("FAIL %s intro out phase midpoint: %d outgoing dark, %d lit\n", cfg.boardName, dark, lit);
                failures++;
            }
        }
    }

    // Through LedDisplay over the view's dark base: front equals the layer alone
    // (Normal over black), indicators keep the base, and it ends on its own.
    {
        CRGB buffer[Board::LED_COUNT];
        LedDisplay display(buffer);
        LedIntroAnimation reference;
        const uint32_t start = 7000;
        const Color l(leftColor.r, leftColor.g, leftColor.b);
        const Color r(rightColor.r, rightColor.g, rightColor.b);

        uint16_t displayElementMap[Board::LED_COUNT];
        for (int i = 0; i < Board::LED_COUNT; i++) displayElementMap[i] = display.elementsAt(i);

        auto setUpDarkBase = [&]() {
            display.setSameSideMode(false);
            display.resetAnimations();
            display.setGlyphsGlyph(Glyph::Empty, Glyph::Empty, Glyph::Empty, Glyph::Empty);
            display.setColonAppearance();
            display.setBorderEnabled(false);
            display.setPlayersIndicatorsState(true);
            display.setIndicatorAppearancePlayerA(l);
            display.setIndicatorAppearancePlayerB(r);
        };
        auto renderAt = [&](const uint32_t ms) {
            for (int i = 0; i < Board::LED_COUNT; i++) buffer[i] = off;
            g_fakeMillis = ms;
            display.render();
        };

        setUpDarkBase();
        g_fakeMillis = start;
        display.startIntro(l, r);
        reference.start(
            start, leftColor, rightColor, displayElementMap,
            LedTarget::Bar, LedTarget::BorderTop, LedTarget::BorderBottom
        );
        const uint32_t total = reference.totalMs();

        for (uint32_t t = 0; t <= total + 20; t += 10) {
            renderAt(start + t);

            CRGB expected[Board::LED_COUNT];
            for (int i = 0; i < Board::LED_COUNT; i++) expected[i] = off;
            reference.render(start + t, expected);
            expected[cfg.indicatorA] = leftColor;
            expected[cfg.indicatorB] = rightColor;

            for (int i = 0; i < Board::LED_COUNT; i++) {
                if (!(buffer[i] == expected[i])) {
                    printf("FAIL %s intro composed t=%u slot %d (%d,%d,%d) expected (%d,%d,%d)\n",
                           cfg.boardName, t, i, buffer[i].r, buffer[i].g, buffer[i].b,
                           expected[i].r, expected[i].g, expected[i].b);
                    failures++;
                }
            }

            if (display.introActive() != (t < total)) {
                printf("FAIL %s intro composed t=%u introActive()=%d\n", cfg.boardName, t, display.introActive());
                failures++;
            }
        }

        // resetAnimations() mid-wipe ends it on the very next frame.
        setUpDarkBase();
        g_fakeMillis = start;
        display.startIntro(l, r);
        renderAt(start + 500);
        display.resetAnimations();
        renderAt(start + 510);
        for (int i = 0; i < Board::LED_COUNT; i++) {
            if (i == cfg.indicatorA || i == cfg.indicatorB) continue;
            if (!(buffer[i] == off)) {
                printf("FAIL %s intro slot %d survived resetAnimations()\n", cfg.boardName, i);
                failures++;
            }
        }
        if (display.introActive()) {
            printf("FAIL %s intro still active after resetAnimations()\n", cfg.boardName);
            failures++;
        }
    }

    printf("intro %s: %d failures\n", cfg.boardName, failures);
    return failures;
}

#endif //LED_DUMP_INTRO_CHECKS_H
