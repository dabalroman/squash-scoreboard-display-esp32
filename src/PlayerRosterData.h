#ifndef PLAYER_ROSTER_DATA_H
#define PLAYER_ROSTER_DATA_H

#include <stdint.h>

/**
 * The roster's NVS blob, with no Arduino dependency so the web save validator and
 * its host test can use it. The layout is persisted: any change needs a versioned
 * migration in PlayerRoster::readBlob(), or an update wipes the roster.
 */

namespace PlayerRosterLimits {
    constexpr uint8_t MAX_PLAYERS = 32;
    constexpr uint8_t NAME_SIZE = 10;      // 9 characters + NUL, matching UserProfile
    constexpr uint8_t BLOB_VERSION = 2;
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

// readBlob() rejects any stored blob whose length differs, so these are load-bearing.
static_assert(sizeof(PlayerEntry) == 17, "PlayerEntry layout is persisted in NVS");
static_assert(sizeof(PlayersData) == 546, "PlayersData layout is persisted in NVS");

namespace PlayerRosterData {
    inline bool isUidTaken(const PlayersData &data, const uint8_t filled, const uint32_t uid) {
        for (uint8_t i = 0; i < filled && i < PlayerRosterLimits::MAX_PLAYERS; i++) {
            if (data.entries[i].uid == uid) {
                return true;
            }
        }

        return false;
    }
}

#endif //PLAYER_ROSTER_DATA_H
