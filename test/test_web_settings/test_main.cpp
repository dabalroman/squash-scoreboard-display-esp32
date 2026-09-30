// POST /settings, /settings/preview and /settings/dev validation (SettingsValidator) and the
// brightness level <-> byte helpers. Board-agnostic. The validator is a .cpp, so it
// is included here: the test envs build no src/*.cpp.
#include <unity.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <Arduino.h>

#include "Web/SettingsValidator.cpp"
#include "Web/WebAccessGate.h"
#include "../common/check.h"
#include "../common/host_globals.h"

void setUp() {}
void tearDown() {}

// Ordered pairs, first match wins - what WebServer::hasArg()/arg() do with a
// repeated key, so duplicates are modelled as the device sees them.
class FakeForm final : public FormLookup {
public:
    std::vector<std::pair<std::string, std::string> > fields;

    FakeForm &add(const std::string &key, const std::string &value) {
        fields.push_back(std::make_pair(key, value));
        return *this;
    }

    FakeForm &set(const std::string &key, const std::string &value) {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].first == key) {
                fields[i].second = value;
                return *this;
            }
        }
        return add(key, value);
    }

    FakeForm &erase(const std::string &key) {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].first == key) {
                fields.erase(fields.begin() + i);
                return *this;
            }
        }
        return *this;
    }

    bool has(const char *key) const override {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].first == key) return true;
        }
        return false;
    }

    std::string get(const char *key) const override {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].first == key) return fields[i].second;
        }
        return std::string();
    }
};

static FakeForm validForm() {
    FakeForm form;
    form.add("level", "5").add("buzzer", "0").add("devMode", "1");
    return form;
}

// Non-default everywhere, WiFi fields included, so a stray write shows.
static PrefsData storedSettings() {
    PrefsData settings;
    settings.brightness = 223;
    settings.buzzerMode = 1;
    settings.enableDevMode = 0;
    strcpy(settings.wifiSSID, "Dom");
    strcpy(settings.wifiPassword, "tajne123");
    return settings;
}

static void assertRejected(const FakeForm &form, const char *expected, const char *what) {
    PrefsData settings = storedSettings();
    const PrefsData before = settings;

    const char *error = SettingsValidator::stage(form, settings);
    TEST_ASSERT_NOT_NULL_MESSAGE(error, strf("%s: accepted, expected \"%s\"", what, expected).c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, error, what);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &settings, sizeof(PrefsData),
                                     strf("%s: settings changed on a rejection", what).c_str());
}

static const char *const BAD_LEVEL = "Błędny poziom jasności.";
static const char *const BAD_BUZZER = "Błędne ustawienie dźwięku.";
static const char *const BAD_DEV_MODE = "Błędne ustawienie trybu deweloperskiego.";

// ---- helpers ------------------------------------------------------------------

static void test_layout_is_the_stored_blob() {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(131, sizeof(PrefsData), "PrefsData size");
}

static void test_level_byte_round_trip() {
    const uint8_t expected[PrefsBrightness::LEVEL_COUNT] = {31, 63, 95, 127, 159, 191, 223, 255};
    for (uint8_t level = 1; level <= PrefsBrightness::LEVEL_COUNT; level++) {
        const uint8_t byte = PrefsBrightness::levelToByte(level);
        CHECK(byte == expected[level - 1], "level %u -> %u, expected %u", level, byte, expected[level - 1]);
        CHECK(PrefsBrightness::byteToLevel(byte) == level, "level %u does not round-trip", level);
    }

    // Any stored byte maps into 1-8, and the default 127 is level 4.
    for (int byte = 0; byte <= 255; byte++) {
        const uint8_t level = PrefsBrightness::byteToLevel(static_cast<uint8_t>(byte));
        CHECK(level >= 1 && level <= 8, "byte %d -> level %u", byte, level);
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, PrefsBrightness::byteToLevel(PrefsData().brightness), "default is level 4");
}

// ---- POST /settings -----------------------------------------------------------

static void test_valid_save_writes_only_the_three_fields() {
    PrefsData settings = storedSettings();
    const char *error = SettingsValidator::stage(validForm(), settings);
    TEST_ASSERT_NULL_MESSAGE(error, error);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(159, settings.brightness, "level 5");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, settings.buzzerMode, "buzzer off");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, settings.enableDevMode, "dev mode on");

    const PrefsData stored = storedSettings();
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(stored.wifiSSID, settings.wifiSSID, sizeof(stored.wifiSSID), "SSID untouched");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(stored.wifiPassword, settings.wifiPassword, sizeof(stored.wifiPassword),
                                     "password untouched");
}

static void test_level_bounds() {
    PrefsData settings = storedSettings();
    TEST_ASSERT_NULL(SettingsValidator::stage(validForm().set("level", "1"), settings));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(31, settings.brightness, "level 1");
    TEST_ASSERT_NULL(SettingsValidator::stage(validForm().set("level", "8"), settings));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(255, settings.brightness, "level 8");

    assertRejected(validForm().set("level", "0"), BAD_LEVEL, "level 0");
    assertRejected(validForm().set("level", "9"), BAD_LEVEL, "level 9");
    assertRejected(validForm().set("level", "10"), BAD_LEVEL, "level 10");
    assertRejected(validForm().set("level", "01"), BAD_LEVEL, "level 01");
    assertRejected(validForm().set("level", ""), BAD_LEVEL, "empty level");
    assertRejected(validForm().set("level", "a"), BAD_LEVEL, "non-numeric level");
    assertRejected(validForm().set("level", "4 "), BAD_LEVEL, "level with a space");
    assertRejected(validForm().set("level", "-1"), BAD_LEVEL, "negative level");
    assertRejected(validForm().set("level", std::string("4\0", 2)), BAD_LEVEL, "level with an embedded NUL");
}

static void test_flag_values() {
    const char *const bad[] = {"3", "", "a", "01", "true", "-1", " 1"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assertRejected(validForm().set("buzzer", bad[i]), BAD_BUZZER, strf("buzzer \"%s\"", bad[i]).c_str());
        assertRejected(validForm().set("devMode", bad[i]), BAD_DEV_MODE, strf("devMode \"%s\"", bad[i]).c_str());
    }
    // Buzzer is tri-state (0 off, 1 always, 2 in match); devMode stays a flag.
    assertRejected(validForm().set("devMode", "2"), BAD_DEV_MODE, "devMode 2");
    {
        PrefsData inMatch = storedSettings();
        TEST_ASSERT_NULL(SettingsValidator::stage(validForm().set("buzzer", "2"), inMatch));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, inMatch.buzzerMode, "buzzer 2");
    }

    PrefsData settings = storedSettings();
    TEST_ASSERT_NULL(SettingsValidator::stage(validForm().set("buzzer", "1").set("devMode", "0"), settings));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, settings.buzzerMode, "buzzer 1");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, settings.enableDevMode, "devMode 0");
}

static void test_missing_fields() {
    assertRejected(validForm().erase("level"), "Brak pola level.", "missing level");
    assertRejected(validForm().erase("buzzer"), BAD_BUZZER, "missing buzzer");
    assertRejected(validForm().erase("devMode"), BAD_DEV_MODE, "missing devMode");
    assertRejected(FakeForm(), "Brak pola level.", "empty body");
}

// WebServer::arg() returns the first occurrence, so the first value decides.
static void test_duplicated_argument_first_wins() {
    PrefsData settings = storedSettings();
    TEST_ASSERT_NULL(SettingsValidator::stage(validForm().add("level", "9"), settings));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(159, settings.brightness, "level=5&level=9 -> 5");

    FakeForm form;
    form.add("level", "9").add("level", "5").add("buzzer", "0").add("devMode", "1");
    assertRejected(form, BAD_LEVEL, "level=9&level=5");
}

// ---- POST /settings/preview ------------------------------------------------------

static void test_preview() {
    bool cancel = true;
    uint8_t brightness = 0;

    FakeForm level;
    level.add("level", "3");
    TEST_ASSERT_NULL(SettingsValidator::validatePreview(level, cancel, brightness));
    TEST_ASSERT_FALSE_MESSAGE(cancel, "level is not a cancel");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(95, brightness, "level 3");

    FakeForm cancelForm;
    cancelForm.add("cancel", "1");
    TEST_ASSERT_NULL(SettingsValidator::validatePreview(cancelForm, cancel, brightness));
    TEST_ASSERT_TRUE_MESSAGE(cancel, "cancel=1");

    FakeForm badCancel;
    badCancel.add("cancel", "0").add("level", "3");
    TEST_ASSERT_EQUAL_STRING("Błędne pole cancel.", SettingsValidator::validatePreview(badCancel, cancel, brightness));

    FakeForm badLevel;
    badLevel.add("level", "9");
    TEST_ASSERT_EQUAL_STRING(BAD_LEVEL, SettingsValidator::validatePreview(badLevel, cancel, brightness));

    TEST_ASSERT_EQUAL_STRING("Brak pola level.", SettingsValidator::validatePreview(FakeForm(), cancel, brightness));
}

// ---- POST /settings/dev ---------------------------------------------------------

static FakeForm devForm(const std::string &ssid, const std::string &password) {
    FakeForm form;
    form.add("ssid", ssid).add("password", password);
    return form;
}

static void assertDevRejected(const FakeForm &form, const bool gateOpen, const char *expected, const char *what) {
    PrefsData settings = storedSettings();
    const PrefsData before = settings;

    const char *error = SettingsValidator::stageDev(form, gateOpen, settings);
    TEST_ASSERT_NOT_NULL_MESSAGE(error, strf("%s: accepted, expected \"%s\"", what, expected).c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, error, what);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &settings, sizeof(PrefsData),
                                     strf("%s: settings changed on a rejection", what).c_str());
}

static const char *const BAD_SSID = "Nazwa sieci Wi-Fi musi mieć od 1 do 63 znaków.";
static const char *const BAD_PASSWORD = "Hasło Wi-Fi może mieć najwyżej 63 znaki.";

static void test_dev_new_network() {
    PrefsData settings = storedSettings();
    TEST_ASSERT_NULL(SettingsValidator::stageDev(devForm("Hala", "kort2024"), false, settings));
    TEST_ASSERT_EQUAL_STRING("Hala", settings.wifiSSID);
    TEST_ASSERT_EQUAL_STRING("kort2024", settings.wifiPassword);

    // Nothing but the two Wi-Fi fields moved, and the arrays are NUL-filled past the text.
    PrefsData expected = storedSettings();
    memset(expected.wifiSSID, 0, sizeof(expected.wifiSSID));
    memset(expected.wifiPassword, 0, sizeof(expected.wifiPassword));
    strcpy(expected.wifiSSID, "Hala");
    strcpy(expected.wifiPassword, "kort2024");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&expected, &settings, sizeof(PrefsData), "only the Wi-Fi fields change");
}

static void test_dev_same_ssid_empty_password_keeps_stored() {
    PrefsData settings = storedSettings();
    TEST_ASSERT_NULL(SettingsValidator::stageDev(devForm("Dom", ""), false, settings));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("tajne123", settings.wifiPassword, "same SSID, empty password keeps it");
    TEST_ASSERT_EQUAL_STRING("Dom", settings.wifiSSID);
}

static void test_dev_new_ssid_empty_password_is_open_network() {
    PrefsData settings = storedSettings();
    TEST_ASSERT_NULL(SettingsValidator::stageDev(devForm("Otwarta", ""), false, settings));
    TEST_ASSERT_EQUAL_STRING("Otwarta", settings.wifiSSID);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", settings.wifiPassword, "new SSID, empty password -> open network");
}

static void test_dev_lengths() {
    PrefsData settings = storedSettings();
    TEST_ASSERT_NULL(SettingsValidator::stageDev(devForm(std::string(63, 's'), std::string(63, 'p')), false, settings));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(63, strlen(settings.wifiSSID), "63-byte SSID");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(63, strlen(settings.wifiPassword), "63-byte password");

    assertDevRejected(devForm("", "haslo123"), false, BAD_SSID, "empty SSID");
    assertDevRejected(devForm(std::string(64, 's'), "haslo123"), false, BAD_SSID, "64-byte SSID");
    assertDevRejected(devForm("Hala", std::string(64, 'p')), false, BAD_PASSWORD, "64-byte password");
    const std::string nul(1, static_cast<char>(0));
    assertDevRejected(devForm("Ha" + nul + "la", "haslo123"), false, BAD_SSID, "SSID with an embedded NUL");
    assertDevRejected(devForm("Hala", "ha" + nul + "slo"), false, BAD_PASSWORD, "password with an embedded NUL");
}

static void test_dev_missing_fields() {
    assertDevRejected(devForm("Hala", "x").erase("ssid"), false, "Brak pola ssid.", "missing ssid");
    assertDevRejected(devForm("Hala", "x").erase("password"), false, "Brak pola password.", "missing password");
    assertDevRejected(FakeForm(), true, "Brak pola ssid.", "empty body");
}

static void test_dev_mode_with_gate_open() {
    PrefsData settings = storedSettings();
    TEST_ASSERT_NULL(SettingsValidator::stageDev(devForm("Dom", "").add("devMode", "1"), true, settings));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, settings.enableDevMode, "devMode 1");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("tajne123", settings.wifiPassword, "toggling Dev Mode keeps the password");

    TEST_ASSERT_NULL(SettingsValidator::stageDev(devForm("Dom", "").add("devMode", "0"), true, settings));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, settings.enableDevMode, "devMode 0");

    // Absent devMode leaves the stored value alone, gate open or not.
    settings.enableDevMode = 1;
    TEST_ASSERT_NULL(SettingsValidator::stageDev(devForm("Dom", ""), true, settings));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, settings.enableDevMode, "no devMode field keeps it");
}

static void test_dev_mode_gate_closed_rejected() {
    const char *const values[] = {"0", "1", "2", ""};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        assertDevRejected(devForm("Hala", "kort2024").add("devMode", values[i]), false,
                          SettingsValidator::DEV_MODE_LOCKED, strf("devMode \"%s\", gate closed", values[i]).c_str());
    }
}

static void test_dev_mode_bad_values() {
    const char *const bad[] = {"2", "", "01", "true", " 1"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assertDevRejected(devForm("Hala", "kort2024").add("devMode", bad[i]), true, BAD_DEV_MODE,
                          strf("devMode \"%s\"", bad[i]).c_str());
    }
}

// ---- WebAccessGate::open() - the one rule behind PlayerSetupWebUi::gated() ----

static void test_access_gate_truth_table() {
    // profileActive alone is always enough, whatever Dev Mode/STA say.
    TEST_ASSERT_TRUE_MESSAGE(WebAccessGate::open(true, false, false), "PROFILE open, dev mode off, STA down");
    TEST_ASSERT_TRUE_MESSAGE(WebAccessGate::open(true, true, false), "PROFILE open, dev mode on, STA down");
    TEST_ASSERT_TRUE_MESSAGE(WebAccessGate::open(true, false, true), "PROFILE open, dev mode off, STA up");
    TEST_ASSERT_TRUE_MESSAGE(WebAccessGate::open(true, true, true), "PROFILE open, dev mode on, STA up");

    // Outside PROFILE, both dev mode AND STA are required.
    TEST_ASSERT_TRUE_MESSAGE(WebAccessGate::open(false, true, true), "PROFILE closed, dev mode on, STA up");
    TEST_ASSERT_FALSE_MESSAGE(WebAccessGate::open(false, true, false), "PROFILE closed, dev mode on, STA down");
    TEST_ASSERT_FALSE_MESSAGE(WebAccessGate::open(false, false, true), "PROFILE closed, dev mode off, STA up");
    TEST_ASSERT_FALSE_MESSAGE(WebAccessGate::open(false, false, false), "PROFILE closed, dev mode off, STA down");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_layout_is_the_stored_blob);
    RUN_TEST(test_level_byte_round_trip);
    RUN_TEST(test_valid_save_writes_only_the_three_fields);
    RUN_TEST(test_level_bounds);
    RUN_TEST(test_flag_values);
    RUN_TEST(test_missing_fields);
    RUN_TEST(test_duplicated_argument_first_wins);
    RUN_TEST(test_preview);
    RUN_TEST(test_dev_new_network);
    RUN_TEST(test_dev_same_ssid_empty_password_keeps_stored);
    RUN_TEST(test_dev_new_ssid_empty_password_is_open_network);
    RUN_TEST(test_dev_lengths);
    RUN_TEST(test_dev_missing_fields);
    RUN_TEST(test_dev_mode_with_gate_open);
    RUN_TEST(test_dev_mode_gate_closed_rejected);
    RUN_TEST(test_dev_mode_bad_values);
    RUN_TEST(test_access_gate_truth_table);
    return UNITY_END();
}
