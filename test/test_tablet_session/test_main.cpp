// TabletSession: the tablet's tournament id, sport and pushed roster (docs/garmin-protocol.md
// 20.2) - BEGIN's checks and the append-only re-push. Board-agnostic.
#include <unity.h>

#include <cstring>
#include <vector>

#include "Tournament/TabletSession.h"
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

    uint32_t crcOf(const std::vector<uint8_t> &w) {
        return Garmin::rosterVersion(static_cast<uint8_t>(w.size() / Garmin::ROSTER_ENTRY_SIZE), w.data());
    }

    bool begin(TabletSession &session, const std::vector<uint8_t> &w, const uint32_t tid = TID) {
        const uint8_t n = static_cast<uint8_t>(w.size() / Garmin::ROSTER_ENTRY_SIZE);
        return session.begin(tid, SQUASH, n, crcOf(w), w.data());
    }

    bool replace(TabletSession &session, const std::vector<uint8_t> &w, const uint32_t tid = TID) {
        const uint8_t n = static_cast<uint8_t>(w.size() / Garmin::ROSTER_ENTRY_SIZE);
        return session.replace(tid, SQUASH, n, crcOf(w), w.data());
    }
}

void test_begin_spec_roster() {
    TabletSession session;
    TEST_ASSERT_FALSE(session.active());
    const std::vector<uint8_t> w = wire({ANNA, KRYSTIAN});
    TEST_ASSERT_TRUE(begin(session, w));
    TEST_ASSERT_TRUE(session.active());
    TEST_ASSERT_EQUAL_HEX32(TID, session.tournamentId());
    TEST_ASSERT_EQUAL_UINT8(SQUASH, session.sport());
    TEST_ASSERT_EQUAL_UINT8(2, session.count());
    TEST_ASSERT_EQUAL_HEX32(0xc694f1d4, session.rosterCrc());
    TEST_ASSERT_EQUAL_MEMORY(w.data(), session.wire(), w.size());

    std::vector<UserProfile *> &p = session.profiles();
    TEST_ASSERT_EQUAL(2, p.size());
    TEST_ASSERT_EQUAL_UINT8(1, p[1]->getId());
    TEST_ASSERT_EQUAL_HEX32(KRYSTIAN.uid, p[1]->getUid());
    TEST_ASSERT_EQUAL_STRING("KRYSTIAN", p[1]->getName());
    TEST_ASSERT_TRUE(p[0]->getColor().r == 0xff && p[0]->getColor().g == 0 && p[0]->getColor().b == 0);

    // Already running: begin() refuses, the session stays.
    TEST_ASSERT_FALSE(begin(session, wire({OLA, ANNA})));
    TEST_ASSERT_EQUAL_HEX32(ANNA.uid, session.profiles()[0]->getUid());

    session.end();
    TEST_ASSERT_FALSE(session.active());
    TEST_ASSERT_EQUAL_UINT8(0, session.count());
    TEST_ASSERT_TRUE(begin(session, wire({OLA, ANNA}), TID + 1));
    TEST_ASSERT_EQUAL_STRING("OLA", session.profiles()[0]->getName());
}

void test_begin_refusals() {
    TabletSession session;
    const std::vector<uint8_t> w = wire({ANNA, KRYSTIAN});
    TEST_ASSERT_FALSE_MESSAGE(session.begin(0, SQUASH, 2, crcOf(w), w.data()), "tid 0");
    TEST_ASSERT_FALSE_MESSAGE(session.begin(TID, 0, 2, crcOf(w), w.data()), "sport none");
    TEST_ASSERT_FALSE_MESSAGE(session.begin(TID, SQUASH, 2, crcOf(w) ^ 1, w.data()), "bad crc");
    // The CRC covers the count: 1 of 2 with the 2-entry CRC fails, and one entry is too few anyway.
    TEST_ASSERT_FALSE_MESSAGE(session.begin(TID, SQUASH, 1, crcOf(w), w.data()), "count mismatch");
    const std::vector<uint8_t> one = wire({ANNA});
    TEST_ASSERT_FALSE_MESSAGE(begin(session, one), "one entry");
    TEST_ASSERT_FALSE_MESSAGE(begin(session, wire({ANNA, {0, 1, 2, 3, "ZERO"}})), "uid 0");
    TEST_ASSERT_FALSE_MESSAGE(begin(session, wire({ANNA, KRYSTIAN, {ANNA.uid, 1, 2, 3, "TWIN"}})), "duplicate uid");
    TEST_ASSERT_FALSE_MESSAGE(begin(session, wire({ANNA, {9, 1, 2, 3, ""}})), "empty name");

    std::vector<uint8_t> bad = w;
    bad[16 + 7 + 2] = 0x7F;
    TEST_ASSERT_FALSE_MESSAGE(begin(session, bad), "DEL in a name");
    bad = w;
    bad[16 + 7 + 1] = 0xC5;
    TEST_ASSERT_FALSE_MESSAGE(begin(session, bad), "non-ASCII in a name");
    bad = wire({ANNA, KRYSTIAN});
    bad[7 + 6] = 'X';   // "ANNA\0\0X..": a byte after the NUL padding
    TEST_ASSERT_FALSE_MESSAGE(begin(session, bad), "garbage after the NUL");

    // 16 entries pass, 17 do not.
    std::vector<Spec> many;
    const char *names[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q"};
    for (uint32_t i = 0; i < 17; i++) many.push_back({0x100 + i, 1, 2, 3, names[i]});
    const std::vector<uint8_t> w17 = wire(many);
    TEST_ASSERT_FALSE_MESSAGE(begin(session, w17), "17 entries");
    many.pop_back();
    TEST_ASSERT_TRUE_MESSAGE(begin(session, wire(many)), "16 entries");
    TEST_ASSERT_EQUAL_UINT8(16, session.count());

    // A 9-character name with no NUL and a name with spaces are fine.
    session.end();
    TEST_ASSERT_TRUE(begin(session, wire({{1, 0, 0, 0, "ABCDEFGHI"}, {2, 0, 0, 0, "J K"}})));
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHI", session.profiles()[0]->getName());
}

void test_replace_appends_in_place() {
    TabletSession session;
    TEST_ASSERT_TRUE(begin(session, wire({ANNA, KRYSTIAN})));
    UserProfile *anna = session.profiles()[0];
    UserProfile *krystian = session.profiles()[1];

    // 16.15 tablet_repush: OLA appended, the CRC is 16.4's rosterVersion.
    TEST_ASSERT_TRUE(replace(session, wire({ANNA, KRYSTIAN, OLA})));
    TEST_ASSERT_EQUAL_UINT8(3, session.count());
    TEST_ASSERT_EQUAL_HEX32(0x150fe75b, session.rosterCrc());
    TEST_ASSERT_TRUE(session.profiles()[0] == anna && session.profiles()[1] == krystian);
    TEST_ASSERT_EQUAL_UINT8(2, session.profiles()[2]->getId());

    // A name fix and a new colour, in place under the same pointer.
    TEST_ASSERT_TRUE(replace(session, wire({{ANNA.uid, 1, 2, 3, "ANIA"}, KRYSTIAN, OLA})));
    TEST_ASSERT_TRUE(session.profiles()[0] == anna);
    TEST_ASSERT_EQUAL_STRING("ANIA", anna->getName());
    TEST_ASSERT_TRUE(anna->getColor().r == 1 && anna->getColor().g == 2 && anna->getColor().b == 3);
    TEST_ASSERT_EQUAL_HEX32(ANNA.uid, anna->getUid());
    TEST_ASSERT_EQUAL_UINT8(0, anna->getId());

    // Unchanged: accepted, nothing changes.
    const uint32_t crc = session.rosterCrc();
    TEST_ASSERT_TRUE(replace(session, wire({{ANNA.uid, 1, 2, 3, "ANIA"}, KRYSTIAN, OLA})));
    TEST_ASSERT_EQUAL_HEX32(crc, session.rosterCrc());
    TEST_ASSERT_EQUAL_UINT8(3, session.count());
}

void test_replace_refusals() {
    TabletSession session;
    TEST_ASSERT_FALSE_MESSAGE(replace(session, wire({ANNA, KRYSTIAN})), "nothing running");
    TEST_ASSERT_TRUE(begin(session, wire({ANNA, KRYSTIAN, OLA})));
    const uint32_t crc = session.rosterCrc();

    TEST_ASSERT_FALSE_MESSAGE(replace(session, wire({ANNA, KRYSTIAN})), "dropped entry");
    TEST_ASSERT_FALSE_MESSAGE(replace(session, wire({ANNA, OLA, KRYSTIAN})), "reordered");
    TEST_ASSERT_FALSE_MESSAGE(replace(session, wire({ANNA, KRYSTIAN, {0x99, 1, 2, 3, "NEW"}})), "replaced uid");
    TEST_ASSERT_FALSE_MESSAGE(replace(session, wire({ANNA, KRYSTIAN, OLA, {0x99, 1, 2, 3, "NEW"}}), TID + 1),
                              "other tournament");
    const std::vector<uint8_t> w = wire({ANNA, KRYSTIAN, OLA});
    TEST_ASSERT_FALSE_MESSAGE(session.replace(TID, SQUASH + 1, 3, crcOf(w), w.data()), "other sport");
    TEST_ASSERT_FALSE_MESSAGE(session.replace(TID, SQUASH, 3, crcOf(w) + 1, w.data()), "bad crc");
    TEST_ASSERT_FALSE_MESSAGE(replace(session, wire({ANNA, KRYSTIAN, OLA, {ANNA.uid, 1, 2, 3, "TWIN"}})), "duplicate");

    TEST_ASSERT_EQUAL_HEX32(crc, session.rosterCrc());
    TEST_ASSERT_EQUAL_UINT8(3, session.count());
    TEST_ASSERT_EQUAL_STRING("OLA", session.profiles()[2]->getName());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_begin_spec_roster);
    RUN_TEST(test_begin_refusals);
    RUN_TEST(test_replace_appends_in_place);
    RUN_TEST(test_replace_refusals);
    return UNITY_END();
}
