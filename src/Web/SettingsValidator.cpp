#include "Web/SettingsValidator.h"

#include <string.h>
#include <string>

namespace {
    // One character, never a number parsed leniently: "01", "1 " and "8x" are refused.
    const char *parseLevel(const FormLookup &form, uint8_t &brightness) {
        if (!form.has("level")) {
            return "Brak pola level.";
        }

        const std::string value = form.get("level");
        if (value.size() != 1 || value[0] < '1' || value[0] > '0' + PrefsBrightness::LEVEL_COUNT) {
            return "Błędny poziom jasności.";
        }

        brightness = PrefsBrightness::levelToByte(static_cast<uint8_t>(value[0] - '0'));
        return nullptr;
    }

    const char *parseBuzzer(const FormLookup &form, uint8_t &mode) {
        const char *error = "Błędne ustawienie dźwięku.";
        if (!form.has("buzzer")) {
            return error;
        }

        const std::string value = form.get("buzzer");
        if (value != "0" && value != "1" && value != "2") {
            return error;
        }

        mode = static_cast<uint8_t>(value[0] - '0');
        return nullptr;
    }

    const char *parseFlag(const FormLookup &form, const char *key, const char *error, uint8_t &flag) {
        if (!form.has(key)) {
            return error;
        }

        const std::string value = form.get(key);
        if (value != "0" && value != "1") {
            return error;
        }

        flag = value == "1" ? 1 : 0;
        return nullptr;
    }
}

const char *SettingsValidator::validate(const FormLookup &form, Patch &out) {
    const char *error = parseLevel(form, out.brightness);
    if (error == nullptr) {
        error = parseBuzzer(form, out.buzzerMode);
    }
    if (error == nullptr) {
        error = parseFlag(form, "devMode", "Błędne ustawienie trybu deweloperskiego.", out.enableDevMode);
    }
    return error;
}

const char *SettingsValidator::stage(const FormLookup &form, PrefsData &settings) {
    Patch patch;
    const char *error = validate(form, patch);
    if (error != nullptr) {
        return error;
    }

    settings.brightness = patch.brightness;
    settings.buzzerMode = patch.buzzerMode;
    settings.enableDevMode = patch.enableDevMode;
    return nullptr;
}

const char *SettingsValidator::validatePreview(const FormLookup &form, bool &cancel, uint8_t &brightness) {
    if (form.has("cancel")) {
        if (form.get("cancel") != "1") {
            return "Błędne pole cancel.";
        }

        cancel = true;
        return nullptr;
    }

    cancel = false;
    return parseLevel(form, brightness);
}

const char *const SettingsValidator::DEV_MODE_LOCKED =
    "Tryb deweloperski można zmienić tylko na ekranie PROFILE albo w trybie deweloperskim w sieci Wi-Fi.";

namespace {
    // Stored as a C string, so an embedded NUL would silently truncate it.
    bool fitsCString(const std::string &value, const size_t minLength, const size_t capacity) {
        return value.size() >= minLength && value.size() < capacity && strlen(value.c_str()) == value.size();
    }
}

const char *SettingsValidator::stageDev(const FormLookup &form, const bool gateOpen, PrefsData &settings) {
    const bool hasDevMode = form.has("devMode");
    if (hasDevMode && !gateOpen) {
        return DEV_MODE_LOCKED;
    }

    if (!form.has("ssid")) {
        return "Brak pola ssid.";
    }
    if (!form.has("password")) {
        return "Brak pola password.";
    }

    const std::string ssid = form.get("ssid");
    if (!fitsCString(ssid, 1, sizeof(settings.wifiSSID))) {
        return "Nazwa sieci Wi-Fi musi mieć od 1 do 63 znaków.";
    }

    const std::string password = form.get("password");
    if (!fitsCString(password, 0, sizeof(settings.wifiPassword))) {
        return "Hasło Wi-Fi może mieć najwyżej 63 znaki.";
    }

    PrefsData staged = settings;
    if (hasDevMode) {
        const char *error = parseFlag(form, "devMode", "Błędne ustawienie trybu deweloperskiego.",
                                      staged.enableDevMode);
        if (error != nullptr) {
            return error;
        }
    }

    const bool keepPassword = password.empty() && ssid == settings.wifiSSID;
    memset(staged.wifiSSID, 0, sizeof(staged.wifiSSID));
    memcpy(staged.wifiSSID, ssid.data(), ssid.size());
    if (!keepPassword) {
        memset(staged.wifiPassword, 0, sizeof(staged.wifiPassword));
        memcpy(staged.wifiPassword, password.data(), password.size());
    }

    settings = staged;
    return nullptr;
}
