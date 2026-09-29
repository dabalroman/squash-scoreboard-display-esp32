#ifndef PREFERENCES_MANAGER_H
#define PREFERENCES_MANAGER_H

#include <Preferences.h>
#include <functional>

#include "PrefsData.h"

#ifdef BOOTSTRAP_WIFI
// Generated into the v1_bootstrap build dir only (on the include path of that env alone),
// so no other env can see it, and it never lands in the source tree.
#include "BootstrapWifi.generated.h"
#endif

class PreferencesManager {
    Preferences preferences;
    std::function<void(const PrefsData &)> applyHandler;

    static constexpr const char *NAMESPACE = "ns";
    static constexpr const char *KEY_SETTINGS = "set";

public:
    PrefsData settings;
    String wifiIpAddress = "";

    PreferencesManager() {
    }

    // Set once from main.cpp: this class never learns about LEDs or the buzzer.
    void setApplyHandler(const std::function<void(const PrefsData &)> &handler) {
        applyHandler = handler;
    }

    // Pushes `settings` to the hardware. Must never block, restart or touch WiFi:
    // it also runs inside POST /connect's save(), just before its restart.
    void apply() {
        if (applyHandler) {
            applyHandler(settings);
        }
    }

    // Returns true when the bootstrap credentials were just seeded (v1_bootstrap only), so
    // main.cpp can log it: this header cannot reach printLn (include cycle).
    bool read() {
        bool valid = false;
        if (preferences.begin(NAMESPACE, true)
            && preferences.getBytesLength(KEY_SETTINGS) == sizeof(PrefsData)
        ) {
            preferences.getBytes(KEY_SETTINGS, &settings, sizeof(PrefsData));
            valid = true;
        }
        preferences.end();

#ifdef BOOTSTRAP_WIFI
        // Only a missing/invalid blob is seeded: a valid one (real credentials, a chosen
        // brightness) is never touched, so re-flashing this env over a used device is safe.
        if (!valid) {
            PrefsBootstrap::seed(settings, BOOTSTRAP_WIFI_SSID, BOOTSTRAP_WIFI_PASSWORD);
            save();
            return true;
        }
#endif
        return false;
    }

    // The apply choke point: every writer (CONFIG exit, web settings, /connect)
    // gets its values live without applying them by hand. Applied even if NVS
    // refuses - the RAM values are what the device runs with either way.
    void save() {
        if (preferences.begin(NAMESPACE, false)) {
            preferences.putBytes(KEY_SETTINGS, &settings, sizeof(PrefsData));
            preferences.end();
        }
        apply();
    }
};

#endif //PREFERENCES_MANAGER_H
