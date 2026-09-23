#ifndef DISPLAY_PROFILE_H
#define DISPLAY_PROFILE_H

#include <FastLED.h>

#include "Board.h"

// Hardware wrapper: the glyph layout follows the board, never its own flag.
#if BOARD_REV == 1
#include "Profiles/SevenSegmentProfile.h"
using ActiveGlyphProfile = SevenSegmentProfile;
#else
#include "Profiles/NineSegmentProfile.h"
using ActiveGlyphProfile = NineSegmentProfile;
#endif

static_assert(ActiveGlyphProfile::PIXELS_USED <= Board::LED_COUNT, "glyph layout exceeds the LED buffer");

/**
 * The one choke point for FastLED.show() on a rendered frame (task #43):
 * scales pixels[] in place for per-segment brightness (V1: ActiveGlyphProfile::
 * compensate is a no-op), then shows. display() and the boot/celebration sweep
 * both call this - never FastLED.show() directly from a render path.
 */
inline void showCompensated(CRGB *pixels) {
    ActiveGlyphProfile::compensate(pixels);
    FastLED.show();
}

#endif //DISPLAY_PROFILE_H
