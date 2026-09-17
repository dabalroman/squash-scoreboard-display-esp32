// V2 (BOARD_REV=2) invariants for the glyph layer, checked on the host:
//   - every write is inside the 74-slot chain
//   - dead slots 12/28/44/60 are never written
//   - digits stay inside their own module (A=58-73, B=42-57, C=26-41, D=10-25)
//     and never touch the centre block (border 0-3/6-9, indicators 4/5)
//   - indicator A writes only slot 5, B only slot 4; the colon writes nothing
// Build: ./check_v2.sh
#include <cstdio>

#include "Display/LedDisplay/LedGlyph.h"

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
        case GlyphId::IndicatorPlayerA: return slot == 5;
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

    printf("%s: %d writes checked, %d failures\n", failures ? "FAIL" : "OK", writes, failures);
    return failures ? 1 : 0;
}
