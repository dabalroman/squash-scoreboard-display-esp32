#ifndef PREFERENCES_MANAGER_H
#define PREFERENCES_MANAGER_H

#include <Preferences.h>

struct PrefsData {
    uint8_t brightness = 127;
    uint8_t enableBuzzer = 1;
    // Default OFF: with an empty or invalid NVS blob a fresh device would otherwise
    // spend ~15 s failing STA against an empty SSID and then raise an open AP.
    // Stored blobs keep their own value, so V1's OTA path is untouched.
    uint8_t enableWifi = 0;
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
