// PlayerRoster against the in-memory Preferences shim: seeding, read-back, the
// players/teams isolation and the rule that only an absent key is ever written.
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include <Arduino.h>

// PlayerPalette.h names Arduino's String in an overload nothing here calls.
struct String {
    size_t length() const { return 0; }
    const char *c_str() const { return ""; }
};

#include "FactoryRosters.h"
#include "PlayerRoster.h"
#include "../common/host_globals.h"

typedef std::vector<uint8_t> Bytes;
typedef PlayerRoster::LoadResult Result;

void setUp() { HostNvs::reset(); }
void tearDown() {}

static Bytes stored(const char *key) {
    return HostNvs::store()["ns"][key];
}

static void put(const char *key, const Bytes &value) {
    HostNvs::store()["ns"][key] = value;
}

static Bytes pattern(const size_t len, const uint8_t seed) {
    Bytes b(len);
    for (size_t i = 0; i < len; i++) b[i] = static_cast<uint8_t>(seed + i * 7);
    return b;
}

static PlayersData blob(const char *firstName, const uint8_t count) {
    PlayersData d;
    memset(&d, 0, sizeof(d));
    d.version = PlayerRosterLimits::BLOB_VERSION;
    d.count = count;
    for (uint8_t i = 0; i < count; i++) {
        d.entries[i].uid = 500u + i;
        strncpy(d.entries[i].name, i == 0 ? firstName : "Other", PlayerRosterLimits::NAME_SIZE - 1);
        d.entries[i].r = 10;
    }
    return d;
}

static Bytes bytesOf(const PlayersData &d) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&d);
    return Bytes(p, p + sizeof(d));
}

static Result loadTeams(PlayerRoster &r) {
    return r.load(FactoryRosters::TEAMS, FactoryRosters::TEAM_COUNT);
}

static Result loadPlayers(PlayerRoster &r) {
    return r.load(FactoryRosters::PLAYERS, FactoryRosters::PLAYER_COUNT);
}

static void assertResult(const Result expected, const Result actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

void test_empty_nvs_seeds_teams_and_writes_them() {
    PlayerRoster teams(FactoryRosters::TEAMS_KEY);
    assertResult(Result::Seeded, loadTeams(teams));
    TEST_ASSERT_EQUAL_UINT8(FactoryRosters::TEAM_COUNT, teams.size());
    TEST_ASSERT_EQUAL_STRING("My", teams.profiles()[0]->getName());
    TEST_ASSERT_EQUAL_STRING("Goscie", teams.profiles()[1]->getName());
    TEST_ASSERT_EQUAL_STRING("Niebieski", teams.profiles()[7]->getName());
    TEST_ASSERT_EQUAL_UINT32(sizeof(PlayersData), stored("tms").size());
    TEST_ASSERT_EQUAL_UINT(1, HostNvs::writeCount());
    TEST_ASSERT_EQUAL_UINT32(0, HostNvs::store()["ns"].count("ply"));
}

void test_absent_key_in_existing_namespace_seeds() {
    put("set", pattern(131, 1));
    PlayerRoster teams(FactoryRosters::TEAMS_KEY);
    assertResult(Result::Seeded, loadTeams(teams));
    TEST_ASSERT_EQUAL_UINT(1, HostNvs::writeCount());
}

void test_valid_blob_read_back_without_rewrite() {
    put("tms", bytesOf(blob("Lwy", 3)));
    PlayerRoster teams(FactoryRosters::TEAMS_KEY);
    assertResult(Result::Valid, loadTeams(teams));
    TEST_ASSERT_EQUAL_UINT8(3, teams.size());
    TEST_ASSERT_EQUAL_STRING("Lwy", teams.profiles()[0]->getName());
    TEST_ASSERT_EQUAL_UINT32(500u, teams.profiles()[0]->getUid());
    TEST_ASSERT_EQUAL_UINT(0, HostNvs::writeCount());
}

void test_seeded_uids_survive_a_reboot() {
    PlayerRoster first(FactoryRosters::TEAMS_KEY);
    loadTeams(first);
    const Bytes afterSeed = stored("tms");

    PlayerRoster second(FactoryRosters::TEAMS_KEY);
    assertResult(Result::Valid, loadTeams(second));
    for (uint8_t i = 0; i < FactoryRosters::TEAM_COUNT; i++) {
        TEST_ASSERT_EQUAL_UINT32(first.profiles()[i]->getUid(), second.profiles()[i]->getUid());
    }
    TEST_ASSERT_TRUE(afterSeed == stored("tms"));
    TEST_ASSERT_EQUAL_UINT(1, HostNvs::writeCount());
}

void test_other_keys_untouched_after_loading_both() {
    const Bytes set = pattern(131, 3);
    const Bytes ply = bytesOf(blob("Adrian", 6));
    const Bytes gar = pattern(174, 9);
    put("set", set);
    put("ply", ply);
    put("gar", gar);

    PlayerRoster players(FactoryRosters::PLAYERS_KEY);
    PlayerRoster teams(FactoryRosters::TEAMS_KEY);
    assertResult(Result::Valid, loadPlayers(players));
    assertResult(Result::Seeded, loadTeams(teams));

    TEST_ASSERT_TRUE(set == stored("set"));
    TEST_ASSERT_TRUE(ply == stored("ply"));
    TEST_ASSERT_TRUE(gar == stored("gar"));
    TEST_ASSERT_EQUAL_UINT(1, HostNvs::writeCount());   // the "tms" seed only
    TEST_ASSERT_EQUAL_UINT32(4, HostNvs::store()["ns"].size());
}

void test_team_save_leaves_players_identical() {
    const Bytes ply = bytesOf(blob("Adrian", 6));
    put("ply", ply);
    PlayerRoster players(FactoryRosters::PLAYERS_KEY);
    PlayerRoster teams(FactoryRosters::TEAMS_KEY);
    loadPlayers(players);
    loadTeams(teams);

    TEST_ASSERT_TRUE(teams.save(blob("Lwy", 4)));
    TEST_ASSERT_TRUE(bytesOf(blob("Lwy", 4)) == stored("tms"));
    TEST_ASSERT_TRUE(teams.resetToDefaults());
    TEST_ASSERT_TRUE(ply == stored("ply"));
}

void test_player_save_leaves_teams_identical() {
    const Bytes tms = bytesOf(blob("Lwy", 4));
    put("tms", tms);
    PlayerRoster players(FactoryRosters::PLAYERS_KEY);
    PlayerRoster teams(FactoryRosters::TEAMS_KEY);
    loadPlayers(players);
    loadTeams(teams);

    TEST_ASSERT_TRUE(players.save(blob("Roman", 5)));
    TEST_ASSERT_TRUE(bytesOf(blob("Roman", 5)) == stored("ply"));
    TEST_ASSERT_TRUE(players.resetToDefaults());
    TEST_ASSERT_TRUE(tms == stored("tms"));
}

// Unreadable: factory list in RAM, nothing written, the stored bytes left as found.
static void checkUnreadable(const Bytes &tms) {
    const Bytes ply = pattern(546, 4);
    put("ply", ply);
    put("tms", tms);
    PlayerRoster teams(FactoryRosters::TEAMS_KEY);
    assertResult(Result::Unreadable, loadTeams(teams));
    TEST_ASSERT_EQUAL_UINT8(FactoryRosters::TEAM_COUNT, teams.size());
    TEST_ASSERT_EQUAL_STRING("My", teams.profiles()[0]->getName());
    TEST_ASSERT_EQUAL_UINT(0, HostNvs::writeCount());
    TEST_ASSERT_TRUE(tms == stored("tms"));
    TEST_ASSERT_TRUE(ply == stored("ply"));
}

void test_wrong_length_writes_nothing() {
    checkUnreadable(pattern(545, 2));
}

void test_empty_value_writes_nothing() {
    checkUnreadable(Bytes());
}

void test_wrong_version_writes_nothing() {
    PlayersData d = blob("Lwy", 2);
    d.version = 1;
    checkUnreadable(bytesOf(d));
}

void test_zero_count_writes_nothing() {
    checkUnreadable(bytesOf(blob("Lwy", 0)));
}

void test_count_over_max_writes_nothing() {
    PlayersData d = blob("Lwy", 2);
    d.count = PlayerRosterLimits::MAX_PLAYERS + 1;
    checkUnreadable(bytesOf(d));
}

void test_begin_fails_writes_nothing() {
    const Bytes ply = bytesOf(blob("Adrian", 6));
    put("ply", ply);
    HostNvs::failBegin() = true;
    PlayerRoster players(FactoryRosters::PLAYERS_KEY);
    PlayerRoster teams(FactoryRosters::TEAMS_KEY);
    assertResult(Result::Unreadable, loadPlayers(players));
    assertResult(Result::Unreadable, loadTeams(teams));
    TEST_ASSERT_EQUAL_UINT8(FactoryRosters::PLAYER_COUNT, players.size());
    TEST_ASSERT_EQUAL_UINT(0, HostNvs::writeCount());
    TEST_ASSERT_TRUE(ply == stored("ply"));
    TEST_ASSERT_EQUAL_UINT32(0, HostNvs::store()["ns"].count("tms"));
}

void test_factory_names_fit_the_fonts() {
    const FactoryPlayer *lists[] = {FactoryRosters::PLAYERS, FactoryRosters::TEAMS};
    const uint8_t counts[] = {FactoryRosters::PLAYER_COUNT, FactoryRosters::TEAM_COUNT};
    for (int l = 0; l < 2; l++) {
        for (uint8_t i = 0; i < counts[l]; i++) {
            const char *name = lists[l][i].name;
            TEST_ASSERT_TRUE(strlen(name) >= 1 && strlen(name) <= PlayerRosterLimits::NAME_SIZE - 1);
            for (const char *c = name; *c; c++) TEST_ASSERT_TRUE(*c >= 0x20 && *c <= 0x7E);
        }
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_nvs_seeds_teams_and_writes_them);
    RUN_TEST(test_absent_key_in_existing_namespace_seeds);
    RUN_TEST(test_valid_blob_read_back_without_rewrite);
    RUN_TEST(test_seeded_uids_survive_a_reboot);
    RUN_TEST(test_other_keys_untouched_after_loading_both);
    RUN_TEST(test_team_save_leaves_players_identical);
    RUN_TEST(test_player_save_leaves_teams_identical);
    RUN_TEST(test_wrong_length_writes_nothing);
    RUN_TEST(test_empty_value_writes_nothing);
    RUN_TEST(test_wrong_version_writes_nothing);
    RUN_TEST(test_zero_count_writes_nothing);
    RUN_TEST(test_count_over_max_writes_nothing);
    RUN_TEST(test_begin_fails_writes_nothing);
    RUN_TEST(test_factory_names_fit_the_fonts);
    return UNITY_END();
}
