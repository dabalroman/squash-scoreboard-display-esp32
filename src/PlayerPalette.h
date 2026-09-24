#ifndef PLAYER_PALETTE_H
#define PLAYER_PALETTE_H

#include <Arduino.h>

#include "Color.h"

/**
 * The colours a player may be given. Board-agnostic.
 *
 * Sixteen entries, all picked by the user on the LEDs (2026-09-24) to replace
 * a screen-derived set whose pale entries washed out to near-white on a WS2812.
 * None reaches the page's bright-colour warning (r+g+b >= 574) or its dark one.
 *
 * Deliberately its own table rather than a subset of `Colors::`. Those are UI
 * accents chosen to look right in a menu; these belong to people.
 *
 * KNOWN CAVEAT, so nobody rediscovers it as a bug: a WS2812 digit seen across a
 * hall mostly conveys hue, and several entries share one. Keep these apart when
 * both players are on court at once:
 *
 *   Pomaranczowy / Brazowy                  the same hue (21 deg), split by lightness
 *   Zielony / Mietowy / Ciemnozielony       120-127 deg
 *   Czerwony / Magenta / Rozowy             0 / 344 / 330 deg
 *   Czerwony / Bordowy                      the same red at two levels
 *
 * Changing an entry does not recolour anyone: players store raw RGB, and the
 * editor matches presets exactly, so an old value shows up as a custom colour.
 *
 * The wire format between the web editor and NVS is the palette *index* for a
 * preset colour, validated as `index < count()`; a custom colour (task #51,
 * picked outside this table) instead travels as #RRGGBB, parsed by fromHex().
 * Either way the page draws real swatches through toHex().
 *
 * The names appear only in the web editor, which is Polish-only and rendered by a
 * phone browser, so they carry real diacritics - this header is UTF-8. They never
 * touch the LED, OLED or e-paper fonts, none of which has an accented glyph.
 *
 * The table is a function-local static, never a `static constexpr` class member:
 * that is an ODR link error on GCC 8.4.
 */

// Named so main.cpp can spell the factory roster out readably.
namespace PlayerColors {
    static constexpr auto Red = Color(0xFF0000);
    static constexpr auto Green = Color(0x00FF00);
    static constexpr auto Yellow = Color(0xFFFF00);
    static constexpr auto Blue = Color(0x14C4FF);
    static constexpr auto Orange = Color(0xFF5900);
    static constexpr auto Purple = Color(0x8A00B4);
    static constexpr auto Cyan = Color(0x0FFF9B);
    static constexpr auto Magenta = Color(0xFF0044);
    static constexpr auto Pink = Color(0xFA3C9B);
    static constexpr auto DarkGreen = Color(0x00B316);
    static constexpr auto Lavender = Color(0xA85EFF);
    static constexpr auto Brown = Color(0xE85A0E);
    static constexpr auto Beige = Color(0xC4B737);
    static constexpr auto Burgundy = Color(0x800000);
    static constexpr auto Mint = Color(0x52FF57);
    static constexpr auto NavyBlue = Color(0x0000FF);
}

namespace PlayerPalette {
    struct PaletteEntry {
        const char *name;
        Color color;
    };

    inline const PaletteEntry *table(uint8_t &count) {
        static const PaletteEntry TABLE[] = {
            {"Czerwony", PlayerColors::Red},
            {"Zielony", PlayerColors::Green},
            {"Żółty", PlayerColors::Yellow},
            {"Niebieski", PlayerColors::Blue},
            {"Pomarańczowy", PlayerColors::Orange},
            {"Purpurowy", PlayerColors::Purple},
            {"Cyjan", PlayerColors::Cyan},
            {"Magenta", PlayerColors::Magenta},
            {"Różowy", PlayerColors::Pink},
            {"Ciemnozielony", PlayerColors::DarkGreen},
            {"Lawendowy", PlayerColors::Lavender},
            {"Brązowy", PlayerColors::Brown},
            {"Beżowy", PlayerColors::Beige},
            {"Bordowy", PlayerColors::Burgundy},
            {"Miętowy", PlayerColors::Mint},
            {"Granatowy", PlayerColors::NavyBlue},
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

    // -1 for anything but 0-9/a-f/A-F, so fromHex can reject in one comparison.
    inline int8_t hexNibble(const char c) {
        if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<int8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<int8_t>(c - 'A' + 10);
        return -1;
    }

    /**
     * Parses "#RRGGBB" (either case), the wire format /preview and /save's custom
     * path both use. Exactly 7 characters; anything else is false and `out` is
     * left untouched, matching the index path's all-or-nothing validation.
     */
    inline bool fromHex(const String &text, Color &out) {
        if (text.length() != 7 || text[0] != '#') {
            return false;
        }

        uint8_t channel[3];
        for (uint8_t i = 0; i < 3; i++) {
            const int8_t hi = hexNibble(text[1 + i * 2]);
            const int8_t lo = hexNibble(text[2 + i * 2]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            channel[i] = static_cast<uint8_t>((hi << 4) | lo);
        }

        out = Color(channel[0], channel[1], channel[2]);
        return true;
    }

    /**
     * True when `c` is exactly a table entry (not merely its nearest one), with
     * that entry's index written to `index` either way - the page uses it to
     * decide whether a stored colour renders as a preset swatch or a custom one.
     */
    inline bool isPreset(const Color c, uint8_t &index) {
        index = nearestIndex(c);
        const PaletteEntry &entry = at(index);
        return entry.color.r == c.r && entry.color.g == c.g && entry.color.b == c.b;
    }
}

#endif //PLAYER_PALETTE_H
