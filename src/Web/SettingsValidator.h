#ifndef SETTINGS_VALIDATOR_H
#define SETTINGS_VALIDATOR_H

#include <stdint.h>

#include "PrefsData.h"
#include "Web/RosterSaveValidator.h"

/**
 * POST /settings, /settings/preview and /settings/dev validation, free of
 * WebServer so the host tests (test/test_web_settings) run it as is.
 */
namespace SettingsValidator {
    struct Patch {
        uint8_t brightness;
        uint8_t enableBuzzer;
        uint8_t enableDevMode;
    };

    // level 1-8, buzzer and devMode exactly "0"/"1", all required. Returns nullptr
    // with `out` filled, or the Polish reason for a 400.
    const char *validate(const FormLookup &form, Patch &out);

    // validate(), then write the three fields into `settings` - only on success, so
    // a rejection leaves it byte-identical.
    const char *stage(const FormLookup &form, PrefsData &settings);

    // cancel=1 (back to the stored brightness), or level=1-8 as the byte to preview.
    const char *validatePreview(const FormLookup &form, bool &cancel, uint8_t &brightness);

    // POST /settings/dev: ssid (1-63 bytes) and password (0-63 bytes) required,
    // devMode optional "0"/"1". An empty password keeps the stored one when the SSID
    // is unchanged (a Dev Mode toggle must not wipe it) and means an open network
    // otherwise. The route itself is ungated - it is a sealed V1's way onto a new
    // network - so devMode is refused unless `gateOpen`. All or nothing: `settings`
    // is written only on success.
    const char *stageDev(const FormLookup &form, bool gateOpen, PrefsData &settings);

    // stageDev()'s refusal of devMode behind a closed gate - a 403, not a 400.
    extern const char *const DEV_MODE_LOCKED;
}

#endif //SETTINGS_VALIDATOR_H
