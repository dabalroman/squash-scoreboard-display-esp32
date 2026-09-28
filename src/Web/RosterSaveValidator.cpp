#include "Web/RosterSaveValidator.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "PlayerPalette.h"

namespace {
    bool isDigits(const std::string &text) {
        for (size_t c = 0; c < text.size(); c++) {
            if (text[c] < '0' || text[c] > '9') {
                return false;
            }
        }

        return true;
    }

    // Same set as Arduino's String::trim(), which this used to be.
    std::string trimmed(const std::string &text) {
        size_t begin = 0;
        size_t end = text.size();

        while (begin < end && isspace(static_cast<unsigned char>(text[begin]))) begin++;
        while (end > begin && isspace(static_cast<unsigned char>(text[end - 1]))) end--;

        return text.substr(begin, end - begin);
    }

    const char *parseColor(const std::string &colorArg, Color &color) {
        // Two wire formats: a palette index, or #RRGGBB for a custom colour.
        if (!colorArg.empty() && colorArg[0] == '#') {
            if (colorArg.size() != 7 || !PlayerPalette::fromHex(colorArg.c_str(), color)) {
                return "Błędny kolor.";
            }
            return nullptr;
        }

        if (colorArg.size() < 1 || colorArg.size() > 2 || !isDigits(colorArg)) {
            return "Błędny kolor.";
        }

        const long colorIndex = atol(colorArg.c_str());
        if (colorIndex < 0 || colorIndex >= PlayerPalette::count()) {
            return "Błędny kolor.";
        }

        color = PlayerPalette::at(static_cast<uint8_t>(colorIndex)).color;
        return nullptr;
    }

    const char *parseUid(const std::string &uidArg, uint32_t &uid) {
        if (uidArg.size() < 1 || uidArg.size() > 10 || !isDigits(uidArg)) {
            return "Błędny identyfikator.";
        }

        uint64_t value = 0;
        for (size_t c = 0; c < uidArg.size(); c++) {
            value = value * 10 + static_cast<uint32_t>(uidArg[c] - '0');
        }

        if (value > 0xFFFFFFFFULL) {
            return "Błędny identyfikator.";
        }

        uid = static_cast<uint32_t>(value);
        return nullptr;
    }
}

const char *RosterSaveValidator::validate(const FormLookup &form, const UidGenerator generateUid, PlayersData &out) {
    if (!form.has("count")) {
        return "Brak pola count.";
    }

    // atol, as String::toInt() did: "3x" still reads as 3.
    const long count = atol(form.get("count").c_str());
    if (count < 1 || count > PlayerRosterLimits::MAX_PLAYERS) {
        return "Pole count musi być w zakresie 1..32.";
    }

    char key[8];

    // An extra name beyond `count` means the page and the body disagree; that is
    // a malformed request, not something to silently truncate.
    snprintf(key, sizeof(key), "n%ld", count);
    if (form.has(key)) {
        return "Więcej imion niż wynosi count.";
    }

    memset(&out, 0, sizeof(out));
    out.version = PlayerRosterLimits::BLOB_VERSION;
    out.count = static_cast<uint8_t>(count);

    for (uint8_t i = 0; i < out.count; i++) {
        snprintf(key, sizeof(key), "n%u", static_cast<unsigned>(i));
        if (!form.has(key)) {
            return "Brakuje imienia w jednym z wierszy.";
        }

        const std::string name = trimmed(form.get(key));
        if (name.size() < 1 || name.size() > PlayerRosterLimits::NAME_SIZE - 1) {
            return "Imię musi mieć od 1 do 9 znaków.";
        }

        for (size_t c = 0; c < name.size(); c++) {
            const unsigned char ch = static_cast<unsigned char>(name[c]);
            if (ch < 0x20 || ch > 0x7E) {
                // These names reach the LED, OLED and e-paper fonts, and none of
                // them has an accented glyph.
                return "Imię bez polskich znaków - tablica ich nie wyświetli.";
            }
        }

        snprintf(key, sizeof(key), "c%u", static_cast<unsigned>(i));
        if (!form.has(key)) {
            return "Brakuje koloru w jednym z wierszy.";
        }

        Color color;
        const char *colorError = parseColor(form.get(key), color);
        if (colorError != nullptr) {
            return colorError;
        }

        memcpy(out.entries[i].name, name.data(), name.size());
        out.entries[i].r = color.r;
        out.entries[i].g = color.g;
        out.entries[i].b = color.b;

        snprintf(key, sizeof(key), "u%u", static_cast<unsigned>(i));
        if (!form.has(key)) {
            return "Brakuje identyfikatora w jednym z wierszy.";
        }

        uint32_t uid = 0;
        const char *uidError = parseUid(form.get(key), uid);
        if (uidError != nullptr) {
            return uidError;
        }

        // Two rows claiming one identity is a malformed body, not something to
        // silently merge - it would make one player vanish into another.
        if (uid != 0 && PlayerRosterData::isUidTaken(out, i, uid)) {
            return "Powtórzony identyfikator gracza.";
        }

        // 0 means "new row". The device mints identities, never the page.
        out.entries[i].uid = uid != 0 ? uid : generateUid(out, i);
    }

    return nullptr;
}
