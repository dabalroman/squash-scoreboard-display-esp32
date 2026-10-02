#ifndef GARMIN_DATA_H
#define GARMIN_DATA_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "GarminProtocol.h"

/**
 * The Garmin App Remote NVS blob (its own key, never a PrefsData field) and the pairing-slot
 * logic over it. Pure, so test_garmin_pairing runs it as is; the Preferences I/O is elsewhere.
 * The slot index is the wire `slotId` (0-7).
 */

namespace GarminLimits {
    constexpr uint8_t BLOB_VERSION = 1;
}

struct GarminSlot {
    uint8_t used;
    uint8_t key[Garmin::KEY_SIZE];
    // LRU counter: bumped to the store-wide maximum + 1 on pairing and on every authentication.
    uint32_t lastUsed;
} __attribute__((packed));

struct GarminData {
    uint8_t version;
    uint8_t enabled;
    // 0 = not generated yet (spec 3: created on the first enable).
    uint32_t boardId;
    GarminSlot slots[Garmin::SLOT_COUNT];
} __attribute__((packed));

// load() rejects a blob of any other length, so a layout change wipes the paired watches.
static_assert(sizeof(GarminSlot) == 21, "GarminSlot layout is persisted in NVS");
static_assert(sizeof(GarminData) == 174, "GarminData layout is persisted in NVS");

namespace GarminPairingStore {
    // esp_random() on the device, a fixed sequence in the tests.
    typedef uint32_t (*RandomSource)();

    constexpr int8_t NO_SLOT = -1;

    inline void fresh(GarminData &data) {
        memset(&data, 0, sizeof(data));
        data.version = GarminLimits::BLOB_VERSION;
    }

    // Exact length and version, else fresh() (feature off, no watches, no board id).
    // Returns whether the stored blob was accepted.
    inline bool load(const uint8_t *blob, const size_t len, GarminData &out) {
        if (blob == nullptr || len != sizeof(GarminData) || blob[0] != GarminLimits::BLOB_VERSION) {
            fresh(out);
            return false;
        }
        memcpy(&out, blob, sizeof(GarminData));

        // A `used` byte other than 0/1 is corruption: drop that slot rather than trust its key.
        uint32_t counters[Garmin::SLOT_COUNT];
        for (uint8_t i = 0; i < Garmin::SLOT_COUNT; i++) {
            if (out.slots[i].used > 1) memset(&out.slots[i], 0, sizeof(GarminSlot));
            counters[i] = out.slots[i].lastUsed;
        }
        // Renumber the LRU counters to their rank (1..n, order and tie-break kept), so max + 1
        // can never wrap to 0 and make the slot just used the next victim.
        for (uint8_t i = 0; i < Garmin::SLOT_COUNT; i++) {
            if (!out.slots[i].used) {
                out.slots[i].lastUsed = 0;
                continue;
            }
            uint32_t rank = 1;
            for (uint8_t j = 0; j < Garmin::SLOT_COUNT; j++) {
                if (j == i || !out.slots[j].used) continue;
                if (counters[j] < counters[i] || (counters[j] == counters[i] && j < i)) rank++;
            }
            out.slots[i].lastUsed = rank;
        }
        return true;
    }

    inline bool isValidBoardId(const uint32_t id) {
        return id != 0 && id != 0xFFFFFFFFu;
    }

    // Draws a board id when the stored one is unusable. Returns true when it changed (persist it).
    inline bool ensureBoardId(GarminData &data, RandomSource random) {
        if (isValidBoardId(data.boardId)) return false;
        uint32_t id;
        do {
            id = random();
        } while (!isValidBoardId(id));
        data.boardId = id;
        return true;
    }

    inline uint8_t pairedCount(const GarminData &data) {
        uint8_t n = 0;
        for (uint8_t i = 0; i < Garmin::SLOT_COUNT; i++) {
            if (data.slots[i].used) n++;
        }
        return n;
    }

    // nullptr for an out-of-range or empty slot (-> UNKNOWN_SLOT).
    inline const uint8_t *keyFor(const GarminData &data, const uint8_t slotId) {
        if (slotId >= Garmin::SLOT_COUNT || !data.slots[slotId].used) return nullptr;
        return data.slots[slotId].key;
    }

    // The first free slot, else the least recently used one (lowest index on a tie).
    inline uint8_t slotForNewKey(const GarminData &data) {
        uint8_t lru = 0;
        for (uint8_t i = 0; i < Garmin::SLOT_COUNT; i++) {
            if (!data.slots[i].used) return i;
            if (data.slots[i].lastUsed < data.slots[lru].lastUsed) lru = i;
        }
        return lru;
    }

    inline uint32_t nextCounter(const GarminData &data) {
        uint32_t max = 0;
        for (uint8_t i = 0; i < Garmin::SLOT_COUNT; i++) {
            if (data.slots[i].used && data.slots[i].lastUsed > max) max = data.slots[i].lastUsed;
        }
        return max + 1;
    }

    // Stores a freshly drawn key, evicting if needed; counts as a use so it is not the next victim.
    inline uint8_t storeKey(GarminData &data, const uint8_t key[Garmin::KEY_SIZE]) {
        const uint8_t slot = slotForNewKey(data);
        const uint32_t counter = nextCounter(data);
        GarminSlot &s = data.slots[slot];
        s.used = 1;
        memcpy(s.key, key, Garmin::KEY_SIZE);
        s.lastUsed = counter;
        return slot;
    }

    // After a successful authentication. False for an empty or out-of-range slot.
    inline bool touch(GarminData &data, const uint8_t slotId) {
        if (keyFor(data, slotId) == nullptr) return false;
        data.slots[slotId].lastUsed = nextCounter(data);
        return true;
    }

    // Keeps the board id and the enabled flag: the board stays the same board.
    inline void forgetAll(GarminData &data) {
        memset(data.slots, 0, sizeof(data.slots));
    }
}

#endif //GARMIN_DATA_H
