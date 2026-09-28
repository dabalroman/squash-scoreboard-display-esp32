// POST /save validation (RosterSaveValidator) and the web JSON string escaper.
// Board-agnostic. The validator is a .cpp, so it is included here: the test envs
// build no src/*.cpp.
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include <Arduino.h>

// PlayerPalette.h names Arduino's String in its String overload of fromHex(),
// which nothing here calls; this is all that overload needs to compile.
struct String {
    size_t length() const { return 0; }
    const char *c_str() const { return ""; }
};

#include "Web/RosterSaveValidator.cpp"
#include "Web/RosterJson.h"
#include "Web/WebJson.h"
#include "../common/check.h"
#include "../common/host_globals.h"

void setUp() {}
void tearDown() {}

class FakeForm final : public FormLookup {
public:
    std::map<std::string, std::string> fields;

    FakeForm &set(const std::string &key, const std::string &value) {
        fields[key] = value;
        return *this;
    }

    FakeForm &erase(const std::string &key) {
        fields.erase(key);
        return *this;
    }

    bool has(const char *key) const override {
        return fields.count(key) != 0;
    }

    std::string get(const char *key) const override {
        const auto it = fields.find(key);
        return it == fields.end() ? std::string() : it->second;
    }
};

static int generatorCalls = 0;

// Deterministic stand-in for PlayerRoster::generateUid (esp_random).
static uint32_t fakeUid(const PlayersData &, const uint8_t filled) {
    generatorCalls++;
    return 1000u + filled;
}

// One valid row per index: "P<i>", palette index <i>, new uid.
static FakeForm validForm(const int count) {
    FakeForm form;
    form.set("count", std::to_string(count));
    for (int i = 0; i < count; i++) {
        form.set("n" + std::to_string(i), "P" + std::to_string(i));
        form.set("c" + std::to_string(i), std::to_string(i % 16));
        form.set("u" + std::to_string(i), "0");
    }
    return form;
}

static const char *validate(const FakeForm &form, PlayersData &out) {
    generatorCalls = 0;
    return RosterSaveValidator::validate(form, &fakeUid, out);
}

static void assertRejected(const FakeForm &form, const char *expected, const char *what) {
    PlayersData out;
    const char *error = validate(form, out);
    TEST_ASSERT_NOT_NULL_MESSAGE(error, strf("%s: accepted, expected \"%s\"", what, expected).c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, error, what);
}

static void assertAccepted(const FakeForm &form, const char *what) {
    PlayersData out;
    const char *error = validate(form, out);
    TEST_ASSERT_NULL_MESSAGE(error, strf("%s: rejected with \"%s\"", what, error ? error : "").c_str());
}

// ---- valid round trips ----------------------------------------------------

static void test_valid_save_stages_the_whole_blob() {
    FakeForm form;
    form.set("count", "3")
        .set("n0", "Ala").set("c0", "0").set("u0", "0")                   // new row, preset
        .set("n1", "  Bob\t").set("c1", "#12aBcD").set("u1", "12345")      // existing, custom colour
        .set("n2", "Krystian9").set("c2", "15").set("u2", "4294967295");   // max name, max uid

    PlayersData out;
    memset(&out, 0xAB, sizeof(out));
    const char *error = validate(form, out);
    TEST_ASSERT_NULL_MESSAGE(error, error);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(PlayerRosterLimits::BLOB_VERSION, out.version, "version");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, out.count, "count");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("Ala", out.entries[0].name, "row 0 name");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Bob", out.entries[1].name, "row 1 name is trimmed");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Krystian9", out.entries[2].name, "row 2 name, 9 characters");

    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, out.entries[0].r, "row 0 = palette 0 (red), r");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, out.entries[0].g, "row 0 = palette 0 (red), g");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, out.entries[0].b, "row 0 = palette 0 (red), b");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x12, out.entries[1].r, "row 1 custom hex, r");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xAB, out.entries[1].g, "row 1 custom hex, g (lower case)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xCD, out.entries[1].b, "row 1 custom hex, b");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, out.entries[2].r, "row 2 = palette 15 (navy), r");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, out.entries[2].g, "row 2 = palette 15 (navy), g");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, out.entries[2].b, "row 2 = palette 15 (navy), b");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1000, out.entries[0].uid, "new row gets a generated uid");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(12345, out.entries[1].uid, "existing uid is kept");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4294967295u, out.entries[2].uid, "2^32-1 is a valid uid");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, generatorCalls, "generator runs only for uid 0");

    // Staged from zero: unused rows and name padding are NUL, never leftovers.
    const PlayersData zero = [] { PlayersData z; memset(&z, 0, sizeof(z)); return z; }();
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&zero.entries[3], &out.entries[3],
                                     sizeof(PlayerEntry) * (PlayerRosterLimits::MAX_PLAYERS - 3),
                                     "rows past count are zero");
    for (size_t c = strlen("Ala"); c < PlayerRosterLimits::NAME_SIZE; c++) {
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('\0', out.entries[0].name[c], "row 0 name is NUL-padded");
    }
}

static void test_valid_save_of_32_new_rows() {
    PlayersData out;
    const char *error = validate(validForm(32), out);
    TEST_ASSERT_NULL_MESSAGE(error, error);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(32, out.count, "count");
    TEST_ASSERT_EQUAL_INT_MESSAGE(32, generatorCalls, "one generated uid per new row");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1031, out.entries[31].uid, "generator sees the rows filled so far");
}

static void test_two_new_rows_both_uid_zero_are_not_duplicates() {
    assertAccepted(validForm(2), "two rows with uid 0");
}

// ---- rejections --------------------------------------------------------------

static void test_rejects_missing_count() {
    assertRejected(validForm(1).erase("count"), "Brak pola count.", "no count");
}

static void test_rejects_count_out_of_range() {
    const char *message = "Pole count musi być w zakresie 1..32.";
    assertRejected(validForm(1).set("count", "0"), message, "count 0");
    assertRejected(validForm(33), message, "count 33");
    assertRejected(validForm(1).set("count", "-1"), message, "count -1");
    assertRejected(validForm(1).set("count", "abc"), message, "count not a number");
    assertRejected(validForm(1).set("count", ""), message, "count empty");
}

static void test_rejects_extra_name() {
    assertRejected(validForm(2).set("count", "1"), "Więcej imion niż wynosi count.", "n1 with count 1");
}

static void test_rejects_missing_name() {
    assertRejected(validForm(2).erase("n1"), "Brakuje imienia w jednym z wierszy.", "no n1");
}

static void test_rejects_missing_colour() {
    assertRejected(validForm(2).erase("c1"), "Brakuje koloru w jednym z wierszy.", "no c1");
}

static void test_rejects_missing_uid() {
    assertRejected(validForm(2).erase("u1"), "Brakuje identyfikatora w jednym z wierszy.", "no u1");
}

static void test_rejects_name_length() {
    const char *message = "Imię musi mieć od 1 do 9 znaków.";
    assertRejected(validForm(1).set("n0", ""), message, "empty name");
    assertRejected(validForm(1).set("n0", "   "), message, "blank name");
    assertRejected(validForm(1).set("n0", "Krystian10"), message, "10 characters");
    assertAccepted(validForm(1).set("n0", " Krystian9 "), "9 characters plus spaces trimmed");
}

static void test_rejects_non_ascii_name() {
    const char *message = "Imię bez polskich znaków - tablica ich nie wyświetli.";
    assertRejected(validForm(1).set("n0", "Łukasz"), message, "UTF-8 letter");
    assertRejected(validForm(1).set("n0", "a\tb"), message, "inner tab");
    assertRejected(validForm(1).set("n0", std::string("a\0b", 3)), message, "embedded NUL");
    assertRejected(validForm(1).set("n0", "a\x7F"), message, "DEL");
}

static void test_rejects_bad_palette_index() {
    const char *message = "Błędny kolor.";
    assertRejected(validForm(1).set("c0", "16"), message, "index 16 of 16");
    assertRejected(validForm(1).set("c0", "99"), message, "index 99");
    assertRejected(validForm(1).set("c0", "100"), message, "three digits");
    assertRejected(validForm(1).set("c0", "-1"), message, "negative");
    assertRejected(validForm(1).set("c0", "1a"), message, "not a number");
    assertRejected(validForm(1).set("c0", ""), message, "empty");
    assertAccepted(validForm(1).set("c0", "15"), "last index");
}

static void test_rejects_bad_hex() {
    const char *message = "Błędny kolor.";
    assertRejected(validForm(1).set("c0", "#"), message, "# alone");
    assertRejected(validForm(1).set("c0", "#12345"), message, "6 characters");
    assertRejected(validForm(1).set("c0", "#1234567"), message, "8 characters");
    assertRejected(validForm(1).set("c0", "#GG0000"), message, "not hex");
    assertRejected(validForm(1).set("c0", "#12 456"), message, "space");
    assertRejected(validForm(1).set("c0", std::string("#12345\0", 7)), message, "embedded NUL");
    assertAccepted(validForm(1).set("c0", "#ffFFff"), "mixed case");
}

static void test_rejects_bad_uid() {
    const char *message = "Błędny identyfikator.";
    assertRejected(validForm(1).set("u0", ""), message, "empty");
    assertRejected(validForm(1).set("u0", "12a"), message, "not a number");
    assertRejected(validForm(1).set("u0", "-1"), message, "negative");
    assertRejected(validForm(1).set("u0", "4294967296"), message, "2^32");
    assertRejected(validForm(1).set("u0", "12345678901"), message, "11 digits");
}

static void test_rejects_duplicate_uid() {
    assertRejected(validForm(3).set("u0", "77").set("u2", "77"), "Powtórzony identyfikator gracza.",
                   "rows 0 and 2 share uid 77");
}

// ---- RosterJson::build() ---------------------------------------------------
// Pure, WebServer/UserProfile-free JSON builder behind GET /api/roster. Split out
// of handleRoster() after a fixed char[12] number buffer there truncated
// ",<uid>]" for a 10-digit uid (esp_random(); about 77% of uids are >= 1e9 and
// need all 10 digits, so ",<uid>]" needs 13 bytes with the NUL) - the closing ']'
// was silently dropped, the JSON was invalid, and web/profile.js's JSON.parse
// threw before the page un-hid anything.

// Small, hand-verifiable palettes - deliberately not PlayerPalette::table()'s real
// 16 entries, so the "exact full string" tests below stay checkable by eye.
static const PlayerPalette::PaletteEntry PALETTE_2[] = {
    {"Czerwony", Color(0xFF0000)},
    {"Niebieski", Color(0x0000FF)},
};
static const PlayerPalette::PaletteEntry PALETTE_1[] = {
    {"Czerwony", Color(0xFF0000)},
};

static void test_roster_json_uid_boundary_values() {
    const RosterJson::Row rows[] = {
        {"A", PALETTE_2[0].color, 4294967295u},   // 2^32-1: 10 digits, the width that overflowed char[12]
        {"B", PALETTE_2[0].color, 0u},
    };

    const std::string out = RosterJson::build(5, PALETTE_2, 2, rows, 2);

    TEST_ASSERT_EQUAL_STRING(
        "{\"max\":5,\"pal\":[[\"Czerwony\",\"#FF0000\"],[\"Niebieski\",\"#0000FF\"]],"
        "\"rows\":[[\"A\",0,4294967295],[\"B\",0,0]]}",
        out.c_str());
}

static void test_roster_json_preset_and_custom_colour_row() {
    const RosterJson::Row rows[] = {
        {"Ala", PALETTE_2[1].color, 1u},              // exact preset match -> index
        {"Bob", Color(0x12, 0xAB, 0xCD), 2u},         // not in the palette -> "#RRGGBB"
    };

    const std::string out = RosterJson::build(32, PALETTE_2, 2, rows, 2);

    TEST_ASSERT_EQUAL_STRING(
        "{\"max\":32,\"pal\":[[\"Czerwony\",\"#FF0000\"],[\"Niebieski\",\"#0000FF\"]],"
        "\"rows\":[[\"Ala\",1,1],[\"Bob\",\"#12ABCD\",2]]}",
        out.c_str());
}

static void test_roster_json_name_with_quote() {
    const RosterJson::Row rows[] = {
        {"Ala\"la", PALETTE_1[0].color, 7u},
    };

    const std::string out = RosterJson::build(1, PALETTE_1, 1, rows, 1);

    TEST_ASSERT_EQUAL_STRING(
        "{\"max\":1,\"pal\":[[\"Czerwony\",\"#FF0000\"]],\"rows\":[[\"Ala\\\"la\",0,7]]}",
        out.c_str());
}

static void test_roster_json_32_rows() {
    uint8_t paletteCount = 0;
    const PlayerPalette::PaletteEntry *palette = PlayerPalette::table(paletteCount);

    RosterJson::Row rows[32];
    char names[32][12];
    for (int i = 0; i < 32; i++) {
        snprintf(names[i], sizeof(names[i]), "P%d", i);
        rows[i].name = names[i];
        rows[i].color = palette[i % paletteCount].color;
        // Every uid here is 10 digits (>= 1e9) - the exact width that overflowed
        // the old char[12] buffer, at the scale (32 rows) the real roster hits.
        rows[i].uid = 1000000000u + static_cast<uint32_t>(i);
    }

    const std::string out = RosterJson::build(32, palette, paletteCount, rows, 32);

    size_t opens = 0, closes = 0, braceOpen = 0, braceClose = 0;
    for (size_t i = 0; i < out.size(); i++) {
        if (out[i] == '[') opens++;
        else if (out[i] == ']') closes++;
        else if (out[i] == '{') braceOpen++;
        else if (out[i] == '}') braceClose++;
    }
    TEST_ASSERT_EQUAL_MESSAGE(opens, closes, "brackets balance");
    TEST_ASSERT_EQUAL_MESSAGE(braceOpen, braceClose, "braces balance");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("]}", out.c_str() + out.size() - 2, "closes cleanly, not truncated");

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out.c_str(), "[\"P0\",0,1000000000]"), out.c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out.c_str(), "[\"P31\",15,1000000031]"), out.c_str());
}

// ---- JSON string escaper ---------------------------------------------------

static std::string json(const char *text) {
    std::string out;
    WebJson::appendString(out, text);
    return out;
}

static void test_json_escaper() {
    TEST_ASSERT_EQUAL_STRING_MESSAGE("\"\"", json("").c_str(), "empty");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("\"Ala\"", json("Ala").c_str(), "plain");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("\"a\\\"b\"", json("a\"b").c_str(), "quote");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("\"a\\\\b\"", json("a\\b").c_str(), "backslash");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("\"\\u003C/script>\"", json("</script>").c_str(), "< never closes a script");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("\"a\\u000Ab\\u0009\\u0001\"", json("a\nb\t\x01").c_str(), "control characters");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("\"Żółty\"", json("Żółty").c_str(), "UTF-8 passes through");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("\"#FF00AA\"", json("#FF00AA").c_str(), "hex colour");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_save_stages_the_whole_blob);
    RUN_TEST(test_valid_save_of_32_new_rows);
    RUN_TEST(test_two_new_rows_both_uid_zero_are_not_duplicates);
    RUN_TEST(test_rejects_missing_count);
    RUN_TEST(test_rejects_count_out_of_range);
    RUN_TEST(test_rejects_extra_name);
    RUN_TEST(test_rejects_missing_name);
    RUN_TEST(test_rejects_missing_colour);
    RUN_TEST(test_rejects_missing_uid);
    RUN_TEST(test_rejects_name_length);
    RUN_TEST(test_rejects_non_ascii_name);
    RUN_TEST(test_rejects_bad_palette_index);
    RUN_TEST(test_rejects_bad_hex);
    RUN_TEST(test_rejects_bad_uid);
    RUN_TEST(test_rejects_duplicate_uid);
    RUN_TEST(test_roster_json_uid_boundary_values);
    RUN_TEST(test_roster_json_preset_and_custom_colour_row);
    RUN_TEST(test_roster_json_name_with_quote);
    RUN_TEST(test_roster_json_32_rows);
    RUN_TEST(test_json_escaper);
    return UNITY_END();
}
