// A tournament's RAM roster (docs/garmin-protocol.md 20.3): BEGIN's checks, the picker's
// selection, join and grow-only growth. Board-agnostic.
#include <unity.h>

#include <cstring>
#include <vector>

#include "Tournament/TournamentRoster.h"
#include "../common/check.h"
#include "../common/host_globals.h"

void setUp() {}
void tearDown() {}

namespace {
    const uint32_t TID = 0x7AB1E001;
    const uint8_t SQUASH = 1;

    struct Spec {
        uint32_t uid;
        uint8_t r, g, b;
        const char *name;
    };

    // Spec 16.4 / 16.15: ANNA, KRYSTIAN, OLA.
    const Spec ANNA = {0x0A0B0C0D, 0xff, 0x00, 0x00, "ANNA"};
    const Spec KRYSTIAN = {0x11223344, 0x00, 0x40, 0xff, "KRYSTIAN"};
    const Spec OLA = {0x55667788, 0x00, 0xc0, 0x30, "OLA"};

    std::vector<uint8_t> wire(const std::vector<Spec> &entries) {
        std::vector<uint8_t> out(entries.size() * Garmin::ROSTER_ENTRY_SIZE);
        for (size_t i = 0; i < entries.size(); i++) {
            const Spec &e = entries[i];
            Garmin::buildRosterEntry(e.uid, e.r, e.g, e.b, e.name, &out[i * Garmin::ROSTER_ENTRY_SIZE]);
        }
        return out;
    }

    uint8_t countOf(const std::vector<uint8_t> &w) {
        return static_cast<uint8_t>(w.size() / Garmin::ROSTER_ENTRY_SIZE);
    }

    uint32_t crcOf(const std::vector<uint8_t> &w) {
        return Garmin::rosterVersion(countOf(w), w.data());
    }

    bool adopt(TournamentRoster &roster, const std::vector<uint8_t> &w) {
        return roster.adopt(countOf(w), crcOf(w), w.data());
    }

    UserProfile stored(const uint8_t id, const Spec &s) {
        return UserProfile(id, s.uid, s.name, Color(s.r, s.g, s.b));
    }
}

void test_valid_refusals() {
    const std::vector<uint8_t> w = wire({ANNA, KRYSTIAN});
    TEST_ASSERT_TRUE(TournamentRoster::valid(2, crcOf(w), w.data()));
    TEST_ASSERT_FALSE_MESSAGE(TournamentRoster::valid(2, crcOf(w) ^ 1, w.data()), "bad crc");
    // The CRC covers the count: 1 of 2 with the 2-entry CRC fails, and one entry is too few anyway.
    TEST_ASSERT_FALSE_MESSAGE(TournamentRoster::valid(1, crcOf(w), w.data()), "count mismatch");
    const std::vector<uint8_t> one = wire({ANNA});
    TEST_ASSERT_FALSE_MESSAGE(TournamentRoster::valid(1, crcOf(one), one.data()), "one entry");
    std::vector<uint8_t> bad = wire({ANNA, {0, 1, 2, 3, "ZERO"}});
    TEST_ASSERT_FALSE_MESSAGE(TournamentRoster::valid(2, crcOf(bad), bad.data()), "uid 0");
    bad = wire({ANNA, KRYSTIAN, {ANNA.uid, 1, 2, 3, "TWIN"}});
    TEST_ASSERT_FALSE_MESSAGE(TournamentRoster::valid(3, crcOf(bad), bad.data()), "duplicate uid");
    bad = wire({ANNA, {9, 1, 2, 3, ""}});
    TEST_ASSERT_FALSE_MESSAGE(TournamentRoster::valid(2, crcOf(bad), bad.data()), "empty name");

    bad = w;
    bad[16 + 7 + 2] = 0x7F;
    TEST_ASSERT_FALSE_MESSAGE(TournamentRoster::valid(2, crcOf(bad), bad.data()), "DEL in a name");
    bad = w;
    bad[16 + 7 + 1] = 0xC5;
    TEST_ASSERT_FALSE_MESSAGE(TournamentRoster::valid(2, crcOf(bad), bad.data()), "non-ASCII in a name");
    bad = w;
    bad[7 + 6] = 'X';   // "ANNA\0\0X..": a byte after the NUL padding
    TEST_ASSERT_FALSE_MESSAGE(TournamentRoster::valid(2, crcOf(bad), bad.data()), "garbage after the NUL");

    // 16 entries pass, 17 do not.
    std::vector<Spec> many;
    const char *names[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q"};
    for (uint32_t i = 0; i < 17; i++) many.push_back({0x100 + i, 1, 2, 3, names[i]});
    const std::vector<uint8_t> w17 = wire(many);
    TEST_ASSERT_FALSE_MESSAGE(TournamentRoster::valid(17, crcOf(w17), w17.data()), "17 entries");
    many.pop_back();
    const std::vector<uint8_t> w16 = wire(many);
    TEST_ASSERT_TRUE_MESSAGE(TournamentRoster::valid(16, crcOf(w16), w16.data()), "16 entries");

    // A 9-character name with no NUL and a name with spaces are fine.
    const std::vector<uint8_t> odd = wire({{1, 0, 0, 0, "ABCDEFGHI"}, {2, 0, 0, 0, "J K"}});
    TEST_ASSERT_TRUE(TournamentRoster::valid(2, crcOf(odd), odd.data()));
}

// The picker on a board: copies with the stored uids, roster_crc over the started entries only.
void test_select_and_commit() {
    TournamentRoster roster;
    const UserProfile anna = stored(0, ANNA);
    const UserProfile krys = stored(1, KRYSTIAN);
    const UserProfile ola = stored(5, OLA);
    TEST_ASSERT_TRUE(roster.select(anna));
    TEST_ASSERT_TRUE(roster.select(krys));
    TEST_ASSERT_TRUE(roster.select(anna));
    TEST_ASSERT_EQUAL_UINT8(2, roster.size());
    TEST_ASSERT_EQUAL_UINT8(0, roster.committedCount());
    TEST_ASSERT_TRUE(roster.profiles()[0] != &anna);
    TEST_ASSERT_EQUAL_HEX32(ANNA.uid, roster.profiles()[0]->getUid());
    TEST_ASSERT_NULL(roster.committedByUid(ANNA.uid));

    TEST_ASSERT_TRUE(roster.unselect(KRYSTIAN.uid));
    TEST_ASSERT_TRUE(roster.select(krys));
    TEST_ASSERT_TRUE(roster.select(ola));
    roster.commit();
    TEST_ASSERT_EQUAL_UINT8(3, roster.committedCount());
    // Spec 16.15: board 1's START from ANNA, KRYSTIAN and OLA.
    TEST_ASSERT_EQUAL_HEX32(0x150fe75b, roster.crc());
    const std::vector<uint8_t> w = wire({ANNA, KRYSTIAN, OLA});
    TEST_ASSERT_EQUAL_MEMORY(w.data(), roster.wire(), w.size());
    TEST_ASSERT_EQUAL_UINT8(5, roster.byUid(OLA.uid)->getId());

    // Started entries stay.
    TEST_ASSERT_FALSE(roster.unselect(ANNA.uid));
    TEST_ASSERT_EQUAL_UINT8(3, roster.size());

    // A stored id already taken by a copy: the lowest free one.
    const UserProfile clash(0, 0x77, "EWA", Color(1, 2, 3));
    TEST_ASSERT_TRUE(roster.select(clash));
    TEST_ASSERT_EQUAL_UINT8(2, roster.byUid(0x77)->getId());
    TEST_ASSERT_EQUAL_HEX32(0x150fe75b, roster.crc());
    TEST_ASSERT_TRUE(roster.unselect(0x77));
}

void test_select_max() {
    TournamentRoster roster;
    for (uint8_t i = 0; i < TournamentRoster::MAX_ENTRIES; i++) {
        TEST_ASSERT_TRUE(roster.select(UserProfile(i, 0x100u + i, "P", Color(1, 2, 3))));
    }
    TEST_ASSERT_FALSE(roster.select(UserProfile(20, 0x200, "Q", Color(1, 2, 3))));
    TEST_ASSERT_EQUAL_UINT8(16, roster.size());
}

// BEGIN on MENU: the peer's roster, ids by index.
void test_adopt_join() {
    TournamentRoster roster;
    const std::vector<uint8_t> w = wire({ANNA, KRYSTIAN});
    TEST_ASSERT_FALSE(roster.adopt(2, crcOf(w) ^ 1, w.data()));
    TEST_ASSERT_EQUAL_UINT8(0, roster.size());
    TEST_ASSERT_TRUE(adopt(roster, w));
    TEST_ASSERT_EQUAL_UINT8(2, roster.committedCount());
    TEST_ASSERT_EQUAL_HEX32(0xc694f1d4, roster.crc());
    TEST_ASSERT_EQUAL_MEMORY(w.data(), roster.wire(), w.size());
    std::vector<UserProfile *> &p = roster.profiles();
    TEST_ASSERT_EQUAL_UINT8(1, p[1]->getId());
    TEST_ASSERT_EQUAL_STRING("KRYSTIAN", p[1]->getName());
    TEST_ASSERT_TRUE(p[0]->getColor().r == 0xff && p[0]->getColor().g == 0 && p[0]->getColor().b == 0);
}

// Growth: appended entries, renames and recolours in place, a started uid never dropped.
void test_adopt_growth() {
    TournamentRoster roster;
    TEST_ASSERT_TRUE(adopt(roster, wire({ANNA, KRYSTIAN})));
    UserProfile *anna = roster.profiles()[0];
    UserProfile *krystian = roster.profiles()[1];

    TEST_ASSERT_TRUE(adopt(roster, wire({ANNA, KRYSTIAN, OLA})));
    TEST_ASSERT_EQUAL_UINT8(3, roster.committedCount());
    TEST_ASSERT_EQUAL_HEX32(0x150fe75b, roster.crc());
    TEST_ASSERT_TRUE(roster.profiles()[0] == anna && roster.profiles()[1] == krystian);
    TEST_ASSERT_EQUAL_UINT8(2, roster.profiles()[2]->getId());

    TEST_ASSERT_TRUE(adopt(roster, wire({{ANNA.uid, 1, 2, 3, "ANIA"}, KRYSTIAN, OLA})));
    TEST_ASSERT_TRUE(roster.profiles()[0] == anna);
    TEST_ASSERT_EQUAL_STRING("ANIA", anna->getName());
    TEST_ASSERT_TRUE(anna->getColor().r == 1 && anna->getColor().g == 2 && anna->getColor().b == 3);
    TEST_ASSERT_EQUAL_UINT8(0, anna->getId());

    // The peer's order is the roster's order.
    TEST_ASSERT_TRUE(adopt(roster, wire({OLA, KRYSTIAN, {ANNA.uid, 1, 2, 3, "ANIA"}})));
    TEST_ASSERT_EQUAL_HEX32(OLA.uid, roster.profiles()[0]->getUid());
    TEST_ASSERT_TRUE(roster.profiles()[2] == anna);

    const uint32_t crc = roster.crc();
    TEST_ASSERT_FALSE_MESSAGE(adopt(roster, wire({ANNA, KRYSTIAN})), "dropped entry");
    TEST_ASSERT_FALSE_MESSAGE(adopt(roster, wire({ANNA, KRYSTIAN, {0x99, 1, 2, 3, "NEW"}})), "replaced uid");
    TEST_ASSERT_EQUAL_HEX32(crc, roster.crc());
    TEST_ASSERT_EQUAL_UINT8(3, roster.size());
}

// A board-side selection not started yet survives a growth, after the pushed entries.
void test_adopt_keeps_selection() {
    TournamentRoster roster;
    TEST_ASSERT_TRUE(roster.select(stored(0, ANNA)));
    TEST_ASSERT_TRUE(roster.select(stored(1, KRYSTIAN)));
    roster.commit();
    TEST_ASSERT_TRUE(roster.select(stored(3, {0x77, 1, 2, 3, "EWA"})));

    TEST_ASSERT_TRUE(adopt(roster, wire({ANNA, KRYSTIAN, OLA})));
    TEST_ASSERT_EQUAL_UINT8(3, roster.committedCount());
    TEST_ASSERT_EQUAL_UINT8(4, roster.size());
    TEST_ASSERT_EQUAL_HEX32(0x77, roster.profiles()[3]->getUid());
    TEST_ASSERT_EQUAL_HEX32(0x150fe75b, roster.crc());
    // OLA took a free id, not the selection's.
    TEST_ASSERT_NOT_EQUAL(roster.profiles()[3]->getId(), roster.profiles()[2]->getId());

    roster.clear();
    TEST_ASSERT_EQUAL_UINT8(0, roster.size());
    TEST_ASSERT_EQUAL_UINT8(0, roster.committedCount());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_refusals);
    RUN_TEST(test_select_and_commit);
    RUN_TEST(test_select_max);
    RUN_TEST(test_adopt_join);
    RUN_TEST(test_adopt_growth);
    RUN_TEST(test_adopt_keeps_selection);
    return UNITY_END();
}
