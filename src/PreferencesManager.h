#ifndef PREFERENCES_MANAGER_H
#define PREFERENCES_MANAGER_H

#include <Preferences.h>

struct PrefsData {
    uint8_t brightness = 127;
    bool enableBuzzer = true;
    bool enableWifi = true;
    char wifiSSID[64] = "";
    char wifiPassword[64] = "";
} __attribute__((packed));

class PreferencesManager {
    Preferences preferences;

    static constexpr const char *NAMESPACE = "ns";
    static constexpr const char *KEY_SETTINGS = "set";

public:
    PrefsData settings;
    String wifiIpAddress = "[ip unknown]";

    PreferencesManager() {
    }

    void read() {
        if (preferences.begin(NAMESPACE, true)
            && preferences.getBytesLength(KEY_SETTINGS) == sizeof(PrefsData)
        ) {
            preferences.getBytes(KEY_SETTINGS, &settings, sizeof(PrefsData));
        }
        preferences.end();
    }

    void save() {
        if (!preferences.begin(NAMESPACE, false)) return;
        preferences.putBytes(KEY_SETTINGS, &settings, sizeof(PrefsData));
        preferences.end();
    }
};

#endif //PREFERENCES_MANAGER_H
