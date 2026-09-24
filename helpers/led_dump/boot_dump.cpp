// Boot sweep frame dump (task #52): one hex line per 5 ms step over a black
// buffer via LedDisplay::renderBootFrame, compared by check_boot.sh against
// goldens equal to the pre-layer LedSweepAnimation::renderFrame (re-baselined
// once, for the ring band 394 -> 690, with that equality re-proven). Permanent
// guard that the boot sweep stays byte-identical.
#include <cstdio>

#include "Display/LedDisplay/LedDisplay.h"

uint32_t g_fakeMillis = 0;
CFastLED FastLED;

int main() {
    const uint32_t duration = LedSweepAnimation::bootParams().durationMs;

    for (uint32_t t = 0; t <= duration; t += 5) {
        // Garbage first: renderBootFrame must lay its own black base.
        CRGB buffer[Board::LED_COUNT];
        for (int i = 0; i < Board::LED_COUNT; i++) buffer[i] = CRGB(9, 9, 9);

        LedDisplay display(buffer);
        display.renderBootFrame(t);

        printf("t=%u", t);
        for (int i = 0; i < Board::LED_COUNT; i++) {
            printf(" %02x%02x%02x", buffer[i].r, buffer[i].g, buffer[i].b);
        }
        printf("\n");
    }
    return 0;
}
