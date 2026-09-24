// V1 (BOARD_REV=1) invariants for LedSweepAnimation, the V1 half of task #45
// (V2's glyph-layer invariants live in check_v2.cpp; V1's glyph layer has no
// separate host check - see golden_v1_frames.txt/golden_v1_glyphs.txt instead):
//   - the boot sweep stays on front slots (0-111 minus back indicators 2/3),
//     ends dark, never blanks a frame, and reaches the colon, digits A/B and C/D
//   - the celebration is a layer over the whole front, history bar included:
//     off the ring the frame equals the plain screen, under every blend
//   - the layer engine (task #52): blend maths, element map, compose, breathing
// Build: ./check_v1.sh
#include <cstdio>

#include "Display/LedDisplay/LedDisplay.h"
#include "layer_checks.h"
#include "sweep_checks.h"

uint32_t g_fakeMillis = 0;
CFastLED FastLED;

// V1 has no dead chain positions - only the two back indicators are reserved.
static bool v1SweepReserved(const int slot) {
    return slot == 2 || slot == 3;
}

int main() {
    int writes = 0;

    const SweepCheckConfig v1Sweep{
        "v1", 112, v1SweepReserved,
        /* indicatorA, indicatorB */ 3, 2,
        /* near (colon)           */ 0, 1,
        /* far A (digits A/B)     */ 46, 87,
        /* far B (digits C/D)     */ 4, 45,
    };
    int failures = runSweepChecks(v1Sweep, writes);

    const LayerCheckConfig v1Layers{
        "v1", /* indicatorA, indicatorB */ 3, 2,
        /* colon, bar, border top/bottom slots */ 2, 24, 0, 0,
    };
    failures += runLayerChecks(v1Layers);

    printf("%s: %d writes checked, %d failures\n", failures ? "FAIL" : "OK", writes, failures);
    return failures ? 1 : 0;
}
