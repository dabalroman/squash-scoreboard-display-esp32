// Garmin App Remote NVS blob and pairing slots (GarminPairingStore): LRU eviction, forget all,
// exact-blob validation, board id generation. Board-agnostic.
#include <unity.h>

#include <cstddef>
#include <cstring>
#include <vector>

#include "Garmin/GarminData.h"
#include "../common/check.h"
#include "../common/host_globals.h"

using namespace GarminPairingStore;

void setUp() {}
void tearDown() {}

namespace {
    std::vector<uint32_t> g_random;
    size_t g_randomAt = 0;
    size_t g_randomCalls = 0;

    uint32_t fakeRandom() {
        g_randomCalls++;
        return g_random[g_randomAt++ % g_random.size()];
    }

    void setRandom(const std::vector<uint32_t> &values) {
        g_random = values;
        g_randomAt = 0;
        g_randomCalls = 0;
    }

    void keyOf(const uint8_t tag, uint8_t key[Garmin::KEY_SIZE]) {
        for (uint8_t i = 0; i < Garmin::KEY_SIZE; i++) key[i] = static_cast<uint8_t>(tag + i);
    }

    uint8_t pair(GarminData &data, const uint8_t tag) {
        uint8_t key[Garmin::KEY_SIZE];
        keyOf(tag, key);
        return storeKey(data, key);
    }

    bool holdsKey(const GarminData &data, const uint8_t slot, const uint8_t tag) {
        uint8_t key[Garmin::KEY_SIZE];
        keyOf(tag, key);
        const uint8_t *stored = keyFor(data, slot);
        return stored != nullptr && memcmp(stored, key, Garmin::KEY_SIZE) == 0;
    }
}

void test_layout() {
    TEST_ASSERT_EQUAL(21, sizeof(GarminSlot));
    TEST_ASSERT_EQUAL(174, sizeof(GarminData));
    TEST_ASSERT_EQUAL(0, offsetof(GarminData, version));
    TEST_ASSERT_EQUAL(1, offsetof(GarminData, enabled));
    TEST_ASSERT_EQUAL(2, offsetof(GarminData, boardId));
    TEST_ASSERT_EQUAL(6, offsetof(GarminData, slots));
}

void test_fresh_defaults() {
    GarminData data;
    memset(&data, 0xAB, sizeof(data));
    fresh(data);
    TEST_ASSERT_EQUAL_UINT8(GarminLimits::BLOB_VERSION, data.version);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, data.enabled, "feature off by default");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, data.boardId, "no board id until the first enable");
    TEST_ASSERT_EQUAL_UINT8(0, pairedCount(data));
    for (uint8_t i = 0; i < Garmin::SLOT_COUNT; i++) TEST_ASSERT_NULL(keyFor(data, i));
}

void test_fills_free_slots_in_order() {
    GarminData data;
    fresh(data);
    for (uint8_t i = 0; i < Garmin::SLOT_COUNT; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, pair(data, static_cast<uint8_t>(i * 16)));
        TEST_ASSERT_EQUAL_UINT8(i + 1, pairedCount(data));
    }
    for (uint8_t i = 0; i < Garmin::SLOT_COUNT; i++) {
        CHECK(holdsKey(data, i, static_cast<uint8_t>(i * 16)), "slot %u key", i);
    }
    TEST_ASSERT_NULL_MESSAGE(keyFor(data, Garmin::SLOT_COUNT), "slot id out of range");
    TEST_ASSERT_NULL(keyFor(data, 255));
}

void test_lru_eviction_order() {
    GarminData data;
    fresh(data);
    for (uint8_t i = 0; i < Garmin::SLOT_COUNT; i++) pair(data, i);

    // Authenticate 0, 3 and 1, in that order: slot 2 is now the least recently used.
    TEST_ASSERT_TRUE(touch(data, 0));
    TEST_ASSERT_TRUE(touch(data, 3));
    TEST_ASSERT_TRUE(touch(data, 1));

    // The 9th pairing evicts 2, then 4, 5, 6, 7 (never touched since pairing), then 0, 3, 1.
    const uint8_t expected[] = {2, 4, 5, 6, 7, 0, 3, 1, 2};
    for (size_t k = 0; k < sizeof(expected); k++) {
        const uint8_t slot = pair(data, static_cast<uint8_t>(100 + k));
        CHECK(slot == expected[k], "eviction %u: slot %u, expected %u", (unsigned) k, slot, expected[k]);
        CHECK(holdsKey(data, slot, static_cast<uint8_t>(100 + k)), "eviction %u key", (unsigned) k);
        TEST_ASSERT_EQUAL_UINT8(Garmin::SLOT_COUNT, pairedCount(data));
    }
}

void test_freed_slot_reused_before_eviction() {
    GarminData data;
    fresh(data);
    for (uint8_t i = 0; i < Garmin::SLOT_COUNT; i++) pair(data, i);
    data.slots[5].used = 0;
    TEST_ASSERT_EQUAL_UINT8(5, slotForNewKey(data));
}

void test_touch_rejects_empty_slots() {
    GarminData data;
    fresh(data);
    TEST_ASSERT_FALSE(touch(data, 0));
    TEST_ASSERT_FALSE(touch(data, Garmin::SLOT_COUNT));
    pair(data, 1);
    TEST_ASSERT_TRUE(touch(data, 0));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, data.slots[0].lastUsed, "pairing is use 1, auth use 2");
}

void test_forget_all() {
    GarminData data;
    fresh(data);
    data.enabled = 1;
    data.boardId = 0x12345678;
    for (uint8_t i = 0; i < 5; i++) pair(data, i);
    forgetAll(data);
    TEST_ASSERT_EQUAL_UINT8(0, pairedCount(data));
    for (uint8_t i = 0; i < Garmin::SLOT_COUNT; i++) {
        TEST_ASSERT_NULL(keyFor(data, i));
        TEST_ASSERT_EQUAL_UINT32(0, data.slots[i].lastUsed);
        for (uint8_t b = 0; b < Garmin::KEY_SIZE; b++) TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, data.slots[i].key[b], "key wiped");
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, data.enabled, "enabled kept");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x12345678, data.boardId, "board id kept");
    TEST_ASSERT_EQUAL_UINT8(0, pair(data, 9));
}

void test_blob_round_trip() {
    GarminData data;
    fresh(data);
    data.enabled = 1;
    data.boardId = 0xCAFEBABE;
    pair(data, 7);
    pair(data, 8);

    uint8_t blob[sizeof(GarminData)];
    memcpy(blob, &data, sizeof(blob));
    GarminData loaded;
    TEST_ASSERT_TRUE(load(blob, sizeof(blob), loaded));
    TEST_ASSERT_EQUAL_MEMORY(&data, &loaded, sizeof(GarminData));
}

void test_blob_rejects() {
    GarminData data;
    fresh(data);
    data.enabled = 1;
    data.boardId = 0xCAFEBABE;
    pair(data, 7);
    std::vector<uint8_t> blob(sizeof(GarminData) + 1);
    memcpy(blob.data(), &data, sizeof(GarminData));

    GarminData loaded;
    TEST_ASSERT_FALSE_MESSAGE(load(blob.data(), sizeof(GarminData) - 1, loaded), "short");
    TEST_ASSERT_EQUAL_UINT8(0, pairedCount(loaded));
    TEST_ASSERT_EQUAL_UINT8(0, loaded.enabled);

    TEST_ASSERT_FALSE_MESSAGE(load(blob.data(), sizeof(GarminData) + 1, loaded), "long");
    TEST_ASSERT_EQUAL_UINT8(0, pairedCount(loaded));

    TEST_ASSERT_FALSE_MESSAGE(load(blob.data(), 0, loaded), "empty");
    TEST_ASSERT_FALSE_MESSAGE(load(nullptr, sizeof(GarminData), loaded), "null");

    blob[0] = GarminLimits::BLOB_VERSION + 1;
    TEST_ASSERT_FALSE_MESSAGE(load(blob.data(), sizeof(GarminData), loaded), "wrong version");
    TEST_ASSERT_EQUAL_UINT8(GarminLimits::BLOB_VERSION, loaded.version);
    TEST_ASSERT_EQUAL_HEX32(0, loaded.boardId);
    TEST_ASSERT_EQUAL_UINT8(0, pairedCount(loaded));
    blob[0] = 0;
    TEST_ASSERT_FALSE_MESSAGE(load(blob.data(), sizeof(GarminData), loaded), "version 0");
}

// A blob with valid length/version but a corrupt `used` byte or a counter at the top of its
// range: the corrupt slot is dropped and the counters renumbered, so LRU still evicts right.
void test_blob_sanitized_on_load() {
    GarminData data;
    fresh(data);
    pair(data, 10);
    pair(data, 20);
    pair(data, 30);
    data.slots[0].lastUsed = 0xFFFFFFFFu;
    data.slots[1].lastUsed = 5;
    data.slots[2].used = 0xAB;

    uint8_t blob[sizeof(GarminData)];
    memcpy(blob, &data, sizeof(blob));
    GarminData loaded;
    TEST_ASSERT_TRUE(load(blob, sizeof(blob), loaded));
    TEST_ASSERT_EQUAL_UINT8(2, pairedCount(loaded));
    TEST_ASSERT_NULL(keyFor(loaded, 2));
    TEST_ASSERT_EQUAL_UINT32(2, loaded.slots[0].lastUsed);
    TEST_ASSERT_EQUAL_UINT32(1, loaded.slots[1].lastUsed);

    // Slot 1 authenticates; slot 0 (oldest after it) must be the victim once all slots are full.
    TEST_ASSERT_TRUE(touch(loaded, 1));
    for (uint8_t tag = 40; pairedCount(loaded) < Garmin::SLOT_COUNT; tag += 10) pair(loaded, tag);
    TEST_ASSERT_EQUAL_UINT8(0, pair(loaded, 200));
    TEST_ASSERT_TRUE(holdsKey(loaded, 1, 20));
}

void test_board_id_generation() {
    GarminData data;
    fresh(data);

    // 0 and 0xFFFFFFFF are skipped until a usable draw.
    setRandom({0, 0xFFFFFFFFu, 0, 0x12345678});
    TEST_ASSERT_TRUE(ensureBoardId(data, fakeRandom));
    TEST_ASSERT_EQUAL_HEX32(0x12345678, data.boardId);
    TEST_ASSERT_EQUAL(4, g_randomCalls);

    // A valid stored id is kept: created once.
    setRandom({0xDEADBEEF});
    TEST_ASSERT_FALSE(ensureBoardId(data, fakeRandom));
    TEST_ASSERT_EQUAL_HEX32(0x12345678, data.boardId);
    TEST_ASSERT_EQUAL(0, g_randomCalls);

    // A stored 0xFFFFFFFF (erased flash pattern) is regenerated.
    data.boardId = 0xFFFFFFFFu;
    setRandom({0xFFFFFFFFu, 1});
    TEST_ASSERT_TRUE(ensureBoardId(data, fakeRandom));
    TEST_ASSERT_EQUAL_HEX32(1, data.boardId);

    TEST_ASSERT_FALSE(isValidBoardId(0));
    TEST_ASSERT_FALSE(isValidBoardId(0xFFFFFFFFu));
    TEST_ASSERT_TRUE(isValidBoardId(0xFFFFFFFEu));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_layout);
    RUN_TEST(test_fresh_defaults);
    RUN_TEST(test_fills_free_slots_in_order);
    RUN_TEST(test_lru_eviction_order);
    RUN_TEST(test_freed_slot_reused_before_eviction);
    RUN_TEST(test_touch_rejects_empty_slots);
    RUN_TEST(test_forget_all);
    RUN_TEST(test_blob_round_trip);
    RUN_TEST(test_blob_rejects);
    RUN_TEST(test_blob_sanitized_on_load);
    RUN_TEST(test_board_id_generation);
    return UNITY_END();
}
