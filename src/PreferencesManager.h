#ifndef PREFERENCES_MANAGER_H
#define PREFERENCES_MANAGER_H

#include <Preferences.h>
#include <functional>

#include "PrefsData.h"

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

    void read() {
        if (preferences.begin(NAMESPACE, true)
            && preferences.getBytesLength(KEY_SETTINGS) == sizeof(PrefsData)
        ) {
            preferences.getBytes(KEY_SETTINGS, &settings, sizeof(PrefsData));
        }
        preferences.end();
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
