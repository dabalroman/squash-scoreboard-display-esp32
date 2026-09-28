#ifndef ROSTER_JSON_H
#define ROSTER_JSON_H

#include <stddef.h>
#include <stdint.h>
#include <cstdio>
#include <string>

#include "Color.h"
#include "PlayerPalette.h"
#include "Web/WebJson.h"

/**
 * Builds the GET /api/roster body:
 *   {"max":N,"pal":[[name,"#RRGGBB"],...],"rows":[[name,colourOrIndex,uid],...]}
 * where a row's colour is a palette index (number) for an exact preset match, or
 * "#RRGGBB" otherwise.
 *
 * Pulled out of handleRoster() as a pure function - no WebServer, no Arduino
 * String, no UserProfile/PlayerRoster/PreferencesManager - so it is a plain
 * transform the host test can call directly with fabricated rows.
 *
 * Root cause this replaces: the old inline builder formatted each row's trailing
 * ",<uid>]" into a fixed `char[12]`. A uint32_t uid (esp_random()) can be 10
 * digits - about 77% of uids are >= 1,000,000,000 - so ",<uid>]" needs up to 13
 * bytes including the NUL. snprintf silently truncated the closing ']', the JSON
 * was invalid, and web/profile.js's JSON.parse threw before the page un-hid
 * anything. `number` below is sized well past the widest value (a uid) so the
 * same class of bug cannot recur unnoticed.
 */
namespace RosterJson {
    struct Row {
        const char *name;
        Color color;
        uint32_t uid;
    };

    // Exact match only (not nearest): a row whose stored colour is not exactly a
    // palette entry - e.g. saved before the palette last changed - must render and
    // re-save as its own hex, never snapped onto the closest preset.
    inline bool exactPresetIndex(const Color c, const PlayerPalette::PaletteEntry *palette,
                                  const uint8_t paletteCount, uint8_t &index) {
        for (uint8_t i = 0; i < paletteCount; i++) {
            if (palette[i].color.r == c.r && palette[i].color.g == c.g && palette[i].color.b == c.b) {
                index = i;
                return true;
            }
        }
        return false;
    }

    inline std::string build(const uint8_t max, const PlayerPalette::PaletteEntry *palette,
                              const uint8_t paletteCount, const Row *rows, const size_t rowCount) {
        std::string out;
        out.reserve(1600);   // 32 custom #RRGGBB rows plus the palette, in one allocation

        // Widest value formatted here is a uid: 10 digits + ",]" + NUL = 13 bytes.
        // Sized well past that so no future field can silently truncate again.
        char number[24];

        out += "{\"max\":";
        snprintf(number, sizeof(number), "%u", static_cast<unsigned>(max));
        out += number;
        out += ",\"pal\":[";

        for (uint8_t i = 0; i < paletteCount; i++) {
            char hex[8];
            PlayerPalette::toHex(palette[i].color, hex);

            if (i > 0) out += ',';
            out += '[';
            WebJson::appendString(out, palette[i].name);
            out += ',';
            WebJson::appendString(out, hex);
            out += ']';
        }

        out += "],\"rows\":[";

        for (size_t i = 0; i < rowCount; i++) {
            uint8_t index = 0;
            const bool preset = exactPresetIndex(rows[i].color, palette, paletteCount, index);

            if (i > 0) out += ',';
            out += '[';
            WebJson::appendString(out, rows[i].name);
            out += ',';
            if (preset) {
                snprintf(number, sizeof(number), "%u", static_cast<unsigned>(index));
                out += number;
            } else {
                char hex[8];
                PlayerPalette::toHex(rows[i].color, hex);
                WebJson::appendString(out, hex);
            }
            // Third field is the identity, carried out to the page and back so a
            // reorder or a rename moves the row without changing who it is.
            snprintf(number, sizeof(number), ",%lu]", static_cast<unsigned long>(rows[i].uid));
            out += number;
        }

        out += "]}";
        return out;
    }
}

#endif //ROSTER_JSON_H
