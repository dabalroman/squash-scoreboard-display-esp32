// Remembered selection per sport (SelectionStore): uid round-trip, deleted and reordered
// profiles, invalid blobs, write-on-change. Board-agnostic.
#include <unity.h>

#include <memory>
#include <vector>

#define LOGGER_HELPER
inline void printLn(const char *, ...) {}

#include "Tournament/Rules/SquashRules.h"
#include "Tournament/SelectionStore.h"
#include "../common/check.h"
#include "../common/host_globals.h"

using namespace SelectionStore;

void setUp() { HostNvs::reset(); }
void tearDown() {}

namespace {
    struct Roster {
        std::vector<std::unique_ptr<UserProfile>> owned;
        std::vector<UserProfile *> profiles;

        void add(const uint32_t uid) {
            owned.emplace_back(new UserProfile(static_cast<uint8_t>(owned.size()), uid, "P", Colors::White));
            profiles.push_back(owned.back().get());
        }
    };

    Roster roster(const std::vector<uint32_t> &uids) {
        Roster r;
        for (const uint32_t uid: uids) r.add(uid);
        return r;
    }

    std::vector<uint32_t> restored(const Sport sport, const std::vector<UserProfile *> &profiles) {
        Tournament tournament(std::unique_ptr<Rules>(new SquashRules()));
        restore(sport, tournament, profiles);
        return uidsOf(tournament.getPlayers());
    }

    void putRaw(const char *key, const std::vector<uint8_t> &bytes) {
        HostNvs::store()[NAMESPACE][key] = bytes;
    }
}

void test_round_trip_keeps_selection_order() {
    Roster r = roster({11, 22, 33});
    TEST_ASSERT_TRUE(save(Sport::Squash, {r.profiles[2], r.profiles[0]}));
    const std::vector<uint32_t> expected = {33, 11};
    TEST_ASSERT_TRUE(restored(Sport::Squash, r.profiles) == expected);
}

void test_sports_are_independent() {
    Roster r = roster({11, 22, 33});
    save(Sport::Padel, {r.profiles[0], r.profiles[1]});
    save(Sport::Volleyball, {r.profiles[1], r.profiles[2]});
    TEST_ASSERT_TRUE(restored(Sport::Squash, r.profiles).empty());
    TEST_ASSERT_TRUE(restored(Sport::Padel, r.profiles) == (std::vector<uint32_t>{11, 22}));
    TEST_ASSERT_TRUE(restored(Sport::Volleyball, r.profiles) == (std::vector<uint32_t>{22, 33}));
}

void test_deleted_profile_dropped() {
    Roster before = roster({11, 22, 33});
    save(Sport::Padel, {before.profiles[0], before.profiles[1]});
    Roster after = roster({22, 33});
    TEST_ASSERT_TRUE(restored(Sport::Padel, after.profiles) == (std::vector<uint32_t>{22}));
}

void test_reordered_roster_keeps_uid_order() {
    Roster before = roster({11, 22, 33});
    save(Sport::Squash, {before.profiles[0], before.profiles[2]});
    Roster after = roster({33, 22, 11});
    const std::vector<UserProfile *> players = resolve(load(Sport::Squash), after.profiles);
    TEST_ASSERT_EQUAL(2, players.size());
    TEST_ASSERT_TRUE(players[0] == after.profiles[2]);
    TEST_ASSERT_TRUE(players[1] == after.profiles[0]);
}

void test_resolve_skips_duplicates() {
    Roster r = roster({11, 22});
    TEST_ASSERT_EQUAL(2, resolve({11, 11, 22, 99}, r.profiles).size());
}

void test_absent_or_invalid_blob_is_empty() {
    Roster r = roster({11, 22});
    TEST_ASSERT_TRUE(load(Sport::Squash).empty());  // namespace missing: read-only begin fails

    putRaw(keyFor(Sport::Squash), std::vector<uint8_t>(10, 1));  // wrong length
    TEST_ASSERT_TRUE(restored(Sport::Squash, r.profiles).empty());

    std::vector<uint8_t> blob(sizeof(SelectionData), 0);
    blob[0] = VERSION + 1;  // unknown version
    blob[1] = 1;
    putRaw(keyFor(Sport::Squash), blob);
    TEST_ASSERT_TRUE(load(Sport::Squash).empty());

    blob[0] = VERSION;
    blob[1] = PlayerRosterLimits::MAX_PLAYERS + 1;  // corrupt count
    putRaw(keyFor(Sport::Squash), blob);
    TEST_ASSERT_TRUE(load(Sport::Squash).empty());
}

void test_unchanged_save_does_not_write() {
    Roster r = roster({11, 22, 33});
    save(Sport::Squash, {r.profiles[0], r.profiles[1]});
    TEST_ASSERT_EQUAL(1, HostNvs::writeCount());
    save(Sport::Squash, {r.profiles[0], r.profiles[1]});
    TEST_ASSERT_EQUAL(1, HostNvs::writeCount());
    save(Sport::Squash, {r.profiles[1], r.profiles[0]});  // order is part of the selection
    TEST_ASSERT_EQUAL(2, HostNvs::writeCount());
}

void test_keys_fit_nvs() {
    TEST_ASSERT_TRUE(strlen(keyFor(Sport::Squash)) <= 15);
    TEST_ASSERT_TRUE(strcmp(keyFor(Sport::Squash), keyFor(Sport::Padel)) != 0);
    TEST_ASSERT_TRUE(strcmp(keyFor(Sport::Padel), keyFor(Sport::Volleyball)) != 0);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_keeps_selection_order);
    RUN_TEST(test_sports_are_independent);
    RUN_TEST(test_deleted_profile_dropped);
    RUN_TEST(test_reordered_roster_keeps_uid_order);
    RUN_TEST(test_resolve_skips_duplicates);
    RUN_TEST(test_absent_or_invalid_blob_is_empty);
    RUN_TEST(test_unchanged_save_does_not_write);
    RUN_TEST(test_keys_fit_nvs);
    return UNITY_END();
}
