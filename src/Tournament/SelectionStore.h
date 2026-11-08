#ifndef SELECTION_STORE_H
#define SELECTION_STORE_H

#include <Preferences.h>
#include <algorithm>
#include <cstring>
#include <vector>

#include "PlayerRosterData.h"
#include "Tournament.h"
#include "UserProfile.h"

/**
 * The players/teams that last started a tournament, one blob per sport, so a rematch needs
 * no toggles. Stored by uid (ids renumber on reorder). Own keys in "ns", never a PrefsData
 * field. loop() only: a watch START reaches save() through the mode's state change.
 */
namespace SelectionStore {
    enum class Sport : uint8_t { Squash, Padel, Volleyball };

    constexpr const char *NAMESPACE = "ns";
    constexpr uint8_t VERSION = 1;

    struct SelectionData {
        uint8_t version;
        uint8_t count;
        uint8_t reserved[2];
        uint32_t uids[PlayerRosterLimits::MAX_PLAYERS];
    };

    // load() rejects any other length, so a layout change only forgets the selection.
    static_assert(sizeof(SelectionData) == 132, "SelectionData layout is persisted in NVS");

    inline const char *keyFor(const Sport sport) {
        switch (sport) {
            case Sport::Squash: return "sel_sq";
            case Sport::Padel: return "sel_pd";
            default: return "sel_vb";  // volleyball and short volleyball share one
        }
    }

    // Stored order; unknown uids dropped (profile deleted since), no duplicates.
    inline std::vector<UserProfile *> resolve(const std::vector<uint32_t> &uids,
                                              const std::vector<UserProfile *> &roster) {
        std::vector<UserProfile *> out;
        for (const uint32_t uid: uids) {
            for (UserProfile *profile: roster) {
                if (profile->getUid() == uid && std::find(out.begin(), out.end(), profile) == out.end()) {
                    out.push_back(profile);
                    break;
                }
            }
        }
        return out;
    }

    inline std::vector<uint32_t> uidsOf(const std::vector<UserProfile *> &players) {
        std::vector<uint32_t> out;
        for (const UserProfile *profile: players) {
            if (out.size() == PlayerRosterLimits::MAX_PLAYERS) break;
            out.push_back(profile->getUid());
        }
        return out;
    }

    // Empty when absent, wrong length, unknown version or corrupt count.
    inline std::vector<uint32_t> load(const Sport sport) {
        SelectionData data{};
        bool ok = false;
        Preferences preferences;
        if (preferences.begin(NAMESPACE, true) && preferences.getBytesLength(keyFor(sport)) == sizeof(data)) {
            ok = preferences.getBytes(keyFor(sport), &data, sizeof(data)) == sizeof(data);
        }
        preferences.end();

        if (!ok || data.version != VERSION || data.count > PlayerRosterLimits::MAX_PLAYERS) {
            return {};
        }
        return std::vector<uint32_t>(data.uids, data.uids + data.count);
    }

    // Writes only on change, so repeat rematches cost no flash wear.
    inline bool save(const Sport sport, const std::vector<UserProfile *> &players) {
        const std::vector<uint32_t> uids = uidsOf(players);
        if (uids == load(sport)) {
            return true;
        }

        SelectionData data{};
        data.version = VERSION;
        data.count = static_cast<uint8_t>(uids.size());
        if (!uids.empty()) {
            memcpy(data.uids, uids.data(), uids.size() * sizeof(uint32_t));
        }

        Preferences preferences;
        if (!preferences.begin(NAMESPACE, false)) {
            return false;
        }
        const size_t written = preferences.putBytes(keyFor(sport), &data, sizeof(data));
        preferences.end();
        return written == sizeof(data);
    }

    inline void restore(const Sport sport, Tournament &tournament, const std::vector<UserProfile *> &roster) {
        for (UserProfile *profile: resolve(load(sport), roster)) {
            tournament.addPlayer(*profile);
        }
    }
}

#endif //SELECTION_STORE_H
