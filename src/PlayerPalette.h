#ifndef PLAYER_PALETTE_H
#define PLAYER_PALETTE_H

#include <Arduino.h>

#include "Color.h"

/**
 * The colours a player may be given. Board-agnostic.
 *
 * Sixteen entries, specified by the user: Sasha Trubetskoy's 20-colour
 * "distinct colours" set without lime, olive, apricot and grey. The numbering
 * below is that set's, which is why it skips 9, 17, 18 and 20.
 *
 * Two entries diverge from it, both because the set was drawn for screens: its
 * green was pushed to 0x00FF00, and its teal - which an LED renders almost
 * identically to cyan - was dropped, its slot reused for the old green.
 *
 * Deliberately its own table rather than a subset of `Colors::`. Those are UI
 * accents chosen to look right in a menu; these belong to people.
 *
 * KNOWN CAVEAT, so nobody rediscovers it as a bug: that set was designed for ink
 * and screens, where lightness and saturation separate colours as well as hue
 * does. A WS2812 digit seen across a hall mostly conveys hue, so the pairs that
 * differ only in level are the ones to watch. Measured closest first:
 *
 *   Rozowy / Lawendowy / Bezowy / Mietowy   four pale entries, all near-white
 *   Pomaranczowy / Brazowy                  7 degrees apart, split by lightness
 *   Czerwony / Bordowy                      the same red at two levels
 *   Niebieski / Granatowy                   the same blue at two levels
 *
 * Those four pale entries also drive all three channels at 81-92%, against 68%
 * for the heaviest saturated colour here and 33% for Zielony - so they cost real
 * battery as well. Keep a pair apart when both players are on court at once, and
 * prefer the saturated entries while the pack is low.
 *
 * The wire format between the web editor and NVS is the palette *index*, not a
 * hex string: validating a saved colour is then `index < count()`, with no hex
 * parser to get wrong, and the page still draws real swatches through toHex().
 *
 * The names appear only in the web editor, which is Polish-only and rendered by a
 * phone browser, so they carry real diacritics - this header is UTF-8. They never
 * touch the LED, OLED or e-paper fonts, none of which has an accented glyph.
 *
 * The table is a function-local static, never a `static constexpr` class member:
 * that is an ODR link error on GCC 8.4.
 */

// Named so main.cpp can spell the factory roster out readably. The trailing
// number is the index in the source set, kept so the list can be checked against it.
namespace PlayerColors {
    static constexpr auto Czerwony = Color(0xE6194B);      //  1 red
    static constexpr auto Zielony = Color(0x00FF00);       //  2 green, pushed to full
                                                           //    saturation: the source set's
                                                           //    3CB44B reads washed out on LEDs
    static constexpr auto Zolty = Color(0xFFE119);         //  3 yellow
    static constexpr auto Niebieski = Color(0x4363D8);     //  4 blue
    static constexpr auto Pomaranczowy = Color(0xF58231);  //  5 orange
    static constexpr auto Purpurowy = Color(0x911EB4);     //  6 purple
    static constexpr auto Cyjan = Color(0x42D4F4);         //  7 cyan
    static constexpr auto Magenta = Color(0xF032E6);       //  8 magenta
    static constexpr auto Rozowy = Color(0xFABED4);        // 10 pink
    static constexpr auto Ciemnozielony = Color(0x3CB44B); // 11 was teal, which was almost
                                                           //    indistinguishable from Cyjan;
                                                           //    reused for the old green
    static constexpr auto Lawendowy = Color(0xDCBEFF);     // 12 lavender
    static constexpr auto Brazowy = Color(0x9A6324);       // 13 brown
    static constexpr auto Bezowy = Color(0xFFFAC8);        // 14 beige
    static constexpr auto Bordowy = Color(0x800000);       // 15 maroon
    static constexpr auto Mietowy = Color(0xAAFFC3);       // 16 mint
    static constexpr auto Granatowy = Color(0x000075);     // 19 navy
}

namespace PlayerPalette {
    struct PaletteEntry {
        const char *name;
        Color color;
    };

    inline const PaletteEntry *table(uint8_t &count) {
        static const PaletteEntry TABLE[] = {
            {"Czerwony", PlayerColors::Czerwony},
            {"Zielony", PlayerColors::Zielony},
            {"Żółty", PlayerColors::Zolty},
            {"Niebieski", PlayerColors::Niebieski},
            {"Pomarańczowy", PlayerColors::Pomaranczowy},
            {"Purpurowy", PlayerColors::Purpurowy},
            {"Cyjan", PlayerColors::Cyjan},
            {"Magenta", PlayerColors::Magenta},
            {"Różowy", PlayerColors::Rozowy},
            {"Ciemnozielony", PlayerColors::Ciemnozielony},
            {"Lawendowy", PlayerColors::Lawendowy},
            {"Brązowy", PlayerColors::Brazowy},
            {"Beżowy", PlayerColors::Bezowy},
            {"Bordowy", PlayerColors::Bordowy},
            {"Miętowy", PlayerColors::Mietowy},
            {"Granatowy", PlayerColors::Granatowy},
        };

        count = static_cast<uint8_t>(sizeof(TABLE) / sizeof(TABLE[0]));
        return TABLE;
    }

    inline uint8_t count() {
        uint8_t n = 0;
        table(n);
        return n;
    }

    inline const PaletteEntry &at(const uint8_t index) {
        uint8_t n = 0;
        const PaletteEntry *entries = table(n);
        return entries[index < n ? index : 0];
    }

    /**
     * The closest entry to an arbitrary colour, by squared RGB distance.
     *
     * Nearest rather than exact on purpose: a roster saved before the palette was
     * last changed holds colours that are no longer presets, and falling back to
     * entry zero would show every one of those players as the same colour - and
     * then save them that way on the next tap. Nearest keeps what was chosen.
     */
    inline uint8_t nearestIndex(const Color color) {
        uint8_t n = 0;
        const PaletteEntry *entries = table(n);

        uint8_t best = 0;
        int32_t bestDistance = INT32_MAX;

        for (uint8_t i = 0; i < n; i++) {
            const int32_t dr = static_cast<int32_t>(entries[i].color.r) - color.r;
            const int32_t dg = static_cast<int32_t>(entries[i].color.g) - color.g;
            const int32_t db = static_cast<int32_t>(entries[i].color.b) - color.b;
            const int32_t distance = dr * dr + dg * dg + db * db;

            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }

        return best;
    }

    inline void toHex(const Color color, char out[8]) {
        snprintf(out, 8, "#%02X%02X%02X", color.r, color.g, color.b);
    }
}

#endif //PLAYER_PALETTE_H
