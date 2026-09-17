#ifndef DISPLAY_PROFILE_H
#define DISPLAY_PROFILE_H

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

#endif //DISPLAY_PROFILE_H
