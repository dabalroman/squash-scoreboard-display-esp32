#ifndef PREFS_DATA_H
#define PREFS_DATA_H

#include <stdint.h>

/**
 * The NVS "set" blob. Split from PreferencesManager.h (which needs <Preferences.h>)
 * so the host tests can stage it.
 */
struct PrefsData {
    uint8_t brightness = 127;
    uint8_t enableBuzzer = 1;
    // Joins the house network at boot, for OTA and telnet without standing at the
    // device. Default OFF: with an empty or invalid NVS blob a fresh device would
    // otherwise spend ~15 s failing STA against an empty SSID and then raise an open
    // AP. Stored blobs keep their own value, so V1's OTA path is untouched.
    uint8_t enableDevMode = 0;
    char wifiSSID[64] = "";
    char wifiPassword[64] = "";
} __attribute__((packed));

// read() rejects a blob of any other length, so a layout change silently drops
// brightness and the WiFi credentials - on a sealed V1 that means no OTA.
static_assert(sizeof(PrefsData) == 131, "PrefsData is the stored NVS layout - never change it");

// The 8 menu levels <-> the stored byte, as ConfigView steps them.
namespace PrefsBrightness {
    enum : uint8_t { LEVEL_COUNT = 8 };

    inline uint8_t levelToByte(const uint8_t level) {
        return static_cast<uint8_t>((level - 1) * 32 + 31);
    }

    inline uint8_t byteToLevel(const uint8_t brightness) {
        return static_cast<uint8_t>(brightness / 32 + 1);
    }
}

#endif //PREFS_DATA_H
