// Boot sweep frames, one hex line per 5 ms step over a garbage-filled buffer,
// byte-identical to golden_boot_v<BOARD_REV>.txt. The goldens equal the pre-layer
// sweep (re-baselined once, for the ring band 394 -> 690, with that equality re-proven).
#include <string>

#include <unity.h>

#include "Display/LedDisplay/LedDisplay.h"
#include "../common/golden.h"
#include "../common/host_globals.h"

void setUp() {}
void tearDown() {}

static std::string bootFrames() {
    std::string out;
    const uint32_t duration = LedSweepAnimation::bootParams().durationMs;

    for (uint32_t t = 0; t <= duration; t += 5) {
        // Garbage first: renderBootFrame must lay its own black base.
        CRGB buffer[Board::LED_COUNT];
        fillPixels(buffer, Board::LED_COUNT, CRGB(9, 9, 9));

        LedDisplay display(buffer);
        display.renderBootFrame(t);

        out += strf("t=%u", t);
        for (int i = 0; i < Board::LED_COUNT; i++) {
            out += strf(" %02x%02x%02x", buffer[i].r, buffer[i].g, buffer[i].b);
        }
        out += "\n";
    }
    return out;
}

static void test_boot_sweep_matches_golden() {
    assertMatchesGolden(BOARD_REV == 1 ? "golden_boot_v1.txt" : "golden_boot_v2.txt", bootFrames());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_boot_sweep_matches_golden);
    return UNITY_END();
}
