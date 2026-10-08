#ifndef PLAYER_ROSTER_H
#define PLAYER_ROSTER_H

#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "FactoryRosters.h"
#include "PlayerPalette.h"
#include "PlayerRosterData.h"
#include "UserProfile.h"
#include "Utils.h"

/**
 * One roster (players or teams), as data rather than code. Board-agnostic - no
 * `#if BOARD_REV`. Each instance owns one NVS key in the "ns" namespace ("ply",
 * "tms" - FactoryRosters.h); both use the same PlayersData layout. The key is
 * deliberately NOT a field in PrefsData: PreferencesManager::read() rejects that
 * blob unless getBytesLength() matches sizeof(PrefsData) exactly, so growing it
 * would silently drop brightness and the WiFi credentials on the first boot
 * after an update.
 *
 * The blob is fixed-size and always written whole. It is accepted only when its
 * length, version and count all check out. load() writes the factory seed only
 * when the key is ABSENT; a key that exists but does not check out (or an NVS that
 * will not open) gets the factory list in RAM only, and NVS is left untouched -
 * only an editor save or restore-factory ever overwrites a stored blob.
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

class PlayerRoster {
public:
    enum class LoadResult : uint8_t {
        Valid,          // stored blob read back
        Seeded,         // key absent: factory list written
        SeedFailed,     // key absent, but the write failed: factory list in RAM
        Unreadable      // key present but invalid, or NVS would not open: RAM only, NVS untouched
    };

private:
    Preferences preferences;

    static constexpr const char *NAMESPACE = "ns";
    const char *const key;
    LoadResult loadResult = LoadResult::Unreadable;

    enum class ReadResult : uint8_t { Valid, Absent, Unreadable };

    const FactoryPlayer *factoryList = nullptr;
    uint8_t factoryCount = 0;

    std::vector<std::unique_ptr<UserProfile>> storage;
    std::vector<UserProfile *> pointers;

public:
    explicit PlayerRoster(const char *key) : key(key) {
    }

    /**
     * A uid that no entry below `filled` already uses. Never 0 - that value is
     * reserved for "this row is new, assign one", which is how the web editor asks
     * for one without being able to mint identities itself.
     */
    static uint32_t generateUid(const PlayersData &data, const uint8_t filled) {
        uint32_t candidate = esp_random();

        // Terminates: at most `filled` (<= 32) values are taken, and each step
        // moves to a different one.
        while (candidate == 0 || PlayerRosterData::isUidTaken(data, filled, candidate)) {
            candidate++;
        }

        return candidate;
    }

    /**
     * setup() only. Reads the stored roster; seeds (and writes) the factory list
     * only when the key does not exist yet.
     */
    LoadResult load(const FactoryPlayer *factory, const uint8_t count) {
        factoryList = factory;
        factoryCount = count > PlayerRosterLimits::MAX_PLAYERS ? PlayerRosterLimits::MAX_PLAYERS : count;

        PlayersData data;

        switch (readBlob(data)) {
            case ReadResult::Valid:
                loadResult = LoadResult::Valid;
                break;
            case ReadResult::Absent:
                seedDefaults(data);
                loadResult = writeBlob(data) ? LoadResult::Seeded : LoadResult::SeedFailed;
                break;
            case ReadResult::Unreadable:
            default:
                seedDefaults(data);
                loadResult = LoadResult::Unreadable;
                break;
        }

        rebuild(data);
        return loadResult;
    }

    LoadResult lastLoadResult() const {
        return loadResult;
    }

    const char *nvsKey() const {
        return key;
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
    ReadResult readBlob(PlayersData &out) {
        if (!preferences.begin(NAMESPACE, true)) {
            preferences.end();
            // A read-only open fails when the namespace does not exist yet (empty NVS).
            // Opening it read-write tells that apart from a broken NVS; it writes no key.
            if (!preferences.begin(NAMESPACE, false)) {
                preferences.end();
                return ReadResult::Unreadable;
            }

            const bool present = preferences.isKey(key);
            preferences.end();
            return present ? ReadResult::Unreadable : ReadResult::Absent;
        }

        ReadResult result;

        if (!preferences.isKey(key)) {
            result = ReadResult::Absent;
        } else {
            const bool ok = preferences.getBytesLength(key) == sizeof(PlayersData)
                            && preferences.getBytes(key, &out, sizeof(PlayersData)) == sizeof(PlayersData)
                            && out.version == PlayerRosterLimits::BLOB_VERSION
                            && out.count >= 1
                            && out.count <= PlayerRosterLimits::MAX_PLAYERS;
            result = ok ? ReadResult::Valid : ReadResult::Unreadable;
        }

        preferences.end();
        return result;
    }

    bool writeBlob(const PlayersData &data) {
        if (!preferences.begin(NAMESPACE, false)) {
            return false;
        }

        const size_t written = preferences.putBytes(key, &data, sizeof(PlayersData));
        preferences.end();

        return written == sizeof(PlayersData);
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
