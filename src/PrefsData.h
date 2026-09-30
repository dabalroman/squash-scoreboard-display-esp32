#ifndef PREFS_DATA_H
#define PREFS_DATA_H

#include <stdint.h>
#include <string.h>

// The stored buzzer byte. 1 is what the old on/off flag stored for "on", so existing
// blobs keep sounding everywhere until changed by hand.
namespace PrefsBuzzer {
    enum : uint8_t { OFF = 0, ALWAYS = 1, IN_MATCH = 2 };

    // Display order of the CONFIG row (NIE -> MECZ -> TAK). An unknown byte is treated as
    // ALWAYS, which is how older firmware reads any non-zero byte.
    inline uint8_t order(const uint8_t mode) {
        return mode == OFF ? 0 : mode == IN_MATCH ? 1 : 2;
    }

    inline uint8_t fromOrder(const uint8_t position) {
        return position == 0 ? OFF : position == 1 ? IN_MATCH : ALWAYS;
    }

    // The byte as the CONFIG tickbox and /api/settings show it: an unknown one reads ALWAYS.
    inline uint8_t normalize(const uint8_t mode) {
        return mode == OFF || mode == IN_MATCH ? mode : ALWAYS;
    }

    inline uint8_t next(const uint8_t mode) {
        return fromOrder(static_cast<uint8_t>((order(mode) + 1) % 3));
    }

    inline uint8_t prev(const uint8_t mode) {
        return fromOrder(static_cast<uint8_t>((order(mode) + 2) % 3));
    }
}

/**
 * The NVS "set" blob. Split from PreferencesManager.h (which needs <Preferences.h>)
 * so the host tests can stage it.
 */
struct PrefsData {
    uint8_t brightness = 127;
    uint8_t buzzerMode = PrefsBuzzer::IN_MATCH;
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

// First-boot seed for the v1_bootstrap env (helpers/wifi_bootstrap.py): the one USB flash of
// a sealed V1 must come up on the house network with OTA, with no remote or menu needed.
// Pure so the host tests can hold it; PreferencesManager::read() decides when to call it.
namespace PrefsBootstrap {
    inline void seed(PrefsData &data, const char *ssid, const char *password) {
        data = PrefsData();
        data.enableDevMode = 1;
        // Bounded copy: the fields are zeroed by the reset above, so [63] stays NUL.
        strncpy(data.wifiSSID, ssid, sizeof(data.wifiSSID) - 1);
        strncpy(data.wifiPassword, password, sizeof(data.wifiPassword) - 1);
    }
}

#endif //PREFS_DATA_H
