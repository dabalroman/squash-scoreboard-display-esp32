#ifndef PREFERENCES_MANAGER_H
#define PREFERENCES_MANAGER_H

#define PREFERENCES_NAMESPACE "ns"
#define PREFERENCES_KEY_SETTINGS "set"

#include <Preferences.h>

struct PrefsData {
    bool enableAp = true;
    bool enableWifi = true;
    uint8_t brightness = 127;
    char wifiSSID[64] = "";
    char wifiPassword[64] = "";
} __attribute__((packed));

class PreferencesManager {
    Preferences preferences;

public:
    PrefsData settings;
    String wifiIpAddress = "[ip unknown]";

    PreferencesManager() {
    }

    void read() {
        if (preferences.begin(PREFERENCES_NAMESPACE, true)
            && preferences.getBytesLength(PREFERENCES_KEY_SETTINGS) == sizeof(PrefsData)
        ) {
            preferences.getBytes(PREFERENCES_KEY_SETTINGS, &settings, sizeof(PrefsData));
        }
        preferences.end();
    }

    void save() {
        if (!preferences.begin(PREFERENCES_NAMESPACE, false)) return;
        preferences.putBytes(PREFERENCES_KEY_SETTINGS, &settings, sizeof(PrefsData));
        preferences.end();
    }
};

#endif //PREFERENCES_MANAGER_H
