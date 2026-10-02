#ifndef GARMIN_STORE_H
#define GARMIN_STORE_H

#include <Preferences.h>

#include "GarminData.h"

/**
 * GarminData in NVS: its own key "gar" in the shared "ns" namespace, never a PrefsData
 * field (PreferencesManager rejects a PrefsData blob of any other size, which would drop
 * the WiFi credentials). loop() only: Preferences is not used from the NimBLE host task.
 */
namespace GarminStore {
    constexpr const char *NAMESPACE = "ns";
    constexpr const char *KEY = "gar";

    // Exact length or fresh (feature off), via GarminPairingStore::load. True when accepted.
    inline bool read(uint8_t *blob, size_t &len) {
        Preferences preferences;
        len = 0;
        if (preferences.begin(NAMESPACE, true) && preferences.getBytesLength(KEY) == sizeof(GarminData)) {
            len = preferences.getBytes(KEY, blob, sizeof(GarminData));
        }
        preferences.end();
        return len == sizeof(GarminData);
    }

    inline bool write(const GarminData &data) {
        Preferences preferences;
        if (!preferences.begin(NAMESPACE, false)) {
            return false;
        }
        const size_t written = preferences.putBytes(KEY, &data, sizeof(GarminData));
        preferences.end();
        return written == sizeof(GarminData);
    }
}

#endif //GARMIN_STORE_H
