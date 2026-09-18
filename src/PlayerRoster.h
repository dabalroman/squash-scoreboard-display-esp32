#ifndef PLAYER_ROSTER_H
#define PLAYER_ROSTER_H

#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <memory>
#include <vector>

#include "PlayerPalette.h"
#include "UserProfile.h"

/**
 * The player roster, as data rather than code. Board-agnostic - no `#if BOARD_REV`.
 *
 * Persistence has its own NVS key ("ply") in the existing "ns" namespace. It is
 * deliberately NOT a field in PrefsData: PreferencesManager::read() rejects that
 * blob unless getBytesLength() matches sizeof(PrefsData) exactly, so growing it
 * would silently drop brightness and the WiFi credentials on the first boot
 * after an update.
 *
 * The blob is fixed-size and always written whole. It is accepted only when its
 * length, version and count all check out; anything else falls back to the
 * factory list (still hardcoded in main.cpp) and writes that back.
 *
 * IDENTITY: every entry carries a `uid`, generated once from the hardware RNG and
 * never reused. It is what makes a player a player - two people called Krystian
 * are two entries with two uids, and renaming, recolouring or reordering the list
 * does not change who anyone is. The `id` handed to UserProfile is just the
 * position in this boot's roster, which is what the LEDs display and what the
 * in-memory match code keys on; it is rebuilt from scratch on every boot.
 *
 * Lifetime: the vectors are built exactly once, by load() in setup(), and by the
 * web editor's save - which always ends in safeRestart(). So nothing can be left
 * holding a stale positional id or a dangling pointer.
 */

namespace PlayerRosterLimits {
    constexpr uint8_t MAX_PLAYERS = 32;
    constexpr uint8_t NAME_SIZE = 10;      // 9 characters + NUL, matching UserProfile
    constexpr uint8_t BLOB_VERSION = 2;    // v2 added the uid
    constexpr uint8_t BLOB_VERSION_LEGACY = 1;
}

struct PlayerEntry {
    uint32_t uid;
    char name[PlayerRosterLimits::NAME_SIZE];
    uint8_t r;
    uint8_t g;
    uint8_t b;
} __attribute__((packed));

struct PlayersData {
    uint8_t version;
    uint8_t count;
    PlayerEntry entries[PlayerRosterLimits::MAX_PLAYERS];
} __attribute__((packed));

// The v1 layout, kept only so a roster saved before uids existed is migrated
// rather than thrown away. Never written - migration always writes v2 back.
struct PlayerEntryV1 {
    char name[PlayerRosterLimits::NAME_SIZE];
    uint8_t r;
    uint8_t g;
    uint8_t b;
} __attribute__((packed));

struct PlayersDataV1 {
    uint8_t version;
    uint8_t count;
    PlayerEntryV1 entries[PlayerRosterLimits::MAX_PLAYERS];
} __attribute__((packed));

// One factory player. The list itself lives in main.cpp, where player profiles
// have always been written. No uid here - those are generated when it is seeded.
struct FactoryPlayer {
    const char *name;
    Color color;
};

class PlayerRoster {
    Preferences preferences;

    static constexpr const char *NAMESPACE = "ns";
    static constexpr const char *KEY_PLAYERS = "ply";

    const FactoryPlayer *factoryList = nullptr;
    uint8_t factoryCount = 0;

    std::vector<std::unique_ptr<UserProfile>> storage;
    std::vector<UserProfile *> pointers;

public:
    /**
     * A uid that no entry below `filled` already uses. Never 0 - that value is
     * reserved for "this row is new, assign one", which is how the web editor asks
     * for one without being able to mint identities itself.
     */
    static uint32_t generateUid(const PlayersData &data, const uint8_t filled) {
        uint32_t candidate = esp_random();

        // Terminates: at most `filled` (<= 32) values are taken, and each step
        // moves to a different one.
        while (candidate == 0 || isUidTaken(data, filled, candidate)) {
            candidate++;
        }

        return candidate;
    }

    static bool isUidTaken(const PlayersData &data, const uint8_t filled, const uint32_t uid) {
        for (uint8_t i = 0; i < filled && i < PlayerRosterLimits::MAX_PLAYERS; i++) {
            if (data.entries[i].uid == uid) {
                return true;
            }
        }

        return false;
    }

    /**
     * setup() only. Reads the stored roster, migrating a v1 blob if it finds one,
     * or seeds the factory list when there is nothing valid to read.
     */
    void load(const FactoryPlayer *factory, const uint8_t count) {
        factoryList = factory;
        factoryCount = count > PlayerRosterLimits::MAX_PLAYERS ? PlayerRosterLimits::MAX_PLAYERS : count;

        PlayersData data;
        bool writeBack = false;

        if (!readBlob(data, writeBack)) {
            seedDefaults(data);
            writeBack = true;
        }

        if (writeBack) {
            writeBlob(data);
        }

        rebuild(data);
    }

    // Every mode takes `std::vector<UserProfile *> &`, so this drops straight in.
    std::vector<UserProfile *> &profiles() {
        return pointers;
    }

    uint8_t size() const {
        return static_cast<uint8_t>(pointers.size());
    }

    // Persist an already-validated blob. The caller reboots; nothing is rebuilt here.
    bool save(const PlayersData &data) {
        return writeBlob(data);
    }

    bool resetToDefaults() {
        PlayersData data;
        seedDefaults(data);
        return writeBlob(data);
    }

private:
    // `migrated` is set when a v1 blob was upgraded and has to be written back.
    bool readBlob(PlayersData &out, bool &migrated) {
        bool ok = false;

        if (preferences.begin(NAMESPACE, true)) {
            const size_t length = preferences.getBytesLength(KEY_PLAYERS);

            if (length == sizeof(PlayersData)) {
                ok = preferences.getBytes(KEY_PLAYERS, &out, sizeof(PlayersData)) == sizeof(PlayersData)
                     && out.version == PlayerRosterLimits::BLOB_VERSION
                     && out.count >= 1
                     && out.count <= PlayerRosterLimits::MAX_PLAYERS;
            } else if (length == sizeof(PlayersDataV1)) {
                PlayersDataV1 legacy;
                if (preferences.getBytes(KEY_PLAYERS, &legacy, sizeof(legacy)) == sizeof(legacy)
                    && legacy.version == PlayerRosterLimits::BLOB_VERSION_LEGACY
                    && legacy.count >= 1
                    && legacy.count <= PlayerRosterLimits::MAX_PLAYERS) {
                    migrateV1(legacy, out);
                    migrated = true;
                    ok = true;
                }
            }
        }

        preferences.end();
        return ok;
    }

    bool writeBlob(const PlayersData &data) {
        if (!preferences.begin(NAMESPACE, false)) {
            return false;
        }

        const size_t written = preferences.putBytes(KEY_PLAYERS, &data, sizeof(PlayersData));
        preferences.end();

        return written == sizeof(PlayersData);
    }

    // Same people, same colours, freshly minted identities - a v1 roster never had
    // any, so this is the one moment where existing players get theirs.
    static void migrateV1(const PlayersDataV1 &legacy, PlayersData &out) {
        memset(&out, 0, sizeof(PlayersData));
        out.version = PlayerRosterLimits::BLOB_VERSION;
        out.count = legacy.count;

        for (uint8_t i = 0; i < legacy.count; i++) {
            memcpy(out.entries[i].name, legacy.entries[i].name, PlayerRosterLimits::NAME_SIZE);
            out.entries[i].name[PlayerRosterLimits::NAME_SIZE - 1] = '\0';
            out.entries[i].r = legacy.entries[i].r;
            out.entries[i].g = legacy.entries[i].g;
            out.entries[i].b = legacy.entries[i].b;
            out.entries[i].uid = generateUid(out, i);
        }
    }

    void seedDefaults(PlayersData &out) const {
        memset(&out, 0, sizeof(PlayersData));
        out.version = PlayerRosterLimits::BLOB_VERSION;
        out.count = factoryCount;

        for (uint8_t i = 0; i < factoryCount; i++) {
            strncpy(out.entries[i].name, factoryList[i].name, PlayerRosterLimits::NAME_SIZE - 1);
            out.entries[i].name[PlayerRosterLimits::NAME_SIZE - 1] = '\0';
            out.entries[i].r = factoryList[i].color.r;
            out.entries[i].g = factoryList[i].color.g;
            out.entries[i].b = factoryList[i].color.b;
            out.entries[i].uid = generateUid(out, i);
        }
    }

    /**
     * Sanitise on load, so a blob written by an older or corrupted build can never
     * put a runaway string on the screens. A POST is rejected instead, never
     * sanitised - the user must see what they typed.
     */
    void rebuild(const PlayersData &data) {
        storage.clear();
        pointers.clear();
        storage.reserve(data.count);
        pointers.reserve(data.count);

        for (uint8_t i = 0; i < data.count; i++) {
            char name[PlayerRosterLimits::NAME_SIZE];
            memcpy(name, data.entries[i].name, PlayerRosterLimits::NAME_SIZE);
            name[PlayerRosterLimits::NAME_SIZE - 1] = '\0';

            for (uint8_t c = 0; name[c] != '\0'; c++) {
                if (name[c] < 0x20 || name[c] > 0x7E) {
                    name[c] = '?';
                }
            }

            if (name[0] == '\0') {
                snprintf(name, sizeof(name), "P%u", static_cast<unsigned>(i));
            }

            const Color color(data.entries[i].r, data.entries[i].g, data.entries[i].b);
            storage.push_back(std::make_unique<UserProfile>(i, data.entries[i].uid, name, color));
            pointers.push_back(storage.back().get());
        }
    }
};

#endif //PLAYER_ROSTER_H
