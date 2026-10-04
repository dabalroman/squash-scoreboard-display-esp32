// Garmin App Remote wire format against docs/garmin-protocol.md section 16, byte for byte,
// plus command parsing, chunk boundaries and the CRC-32 check value. Board-agnostic.
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "Garmin/GarminProtocol.h"
#include "Garmin/WatchCommand.h"
#include "Garmin/WatchState.h"
#include "../common/check.h"
#include "../common/host_globals.h"

using namespace Garmin;

void setUp() {}
void tearDown() {}

namespace {
    std::vector<uint8_t> hex(const char *text) {
        std::vector<uint8_t> out;
        int nibble = -1;
        for (const char *p = text; *p; p++) {
            int v;
            if (*p >= '0' && *p <= '9') v = *p - '0';
            else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
            else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
            else continue;
            if (nibble < 0) {
                nibble = v;
            } else {
                out.push_back(static_cast<uint8_t>(nibble << 4 | v));
                nibble = -1;
            }
        }
        return out;
    }

    std::string dump(const uint8_t *data, const size_t len) {
        std::string s;
        for (size_t i = 0; i < len; i++) s += strf(i ? " %02x" : "%02x", data[i]);
        return s;
    }

    void expectBytes(const char *expectedHex, const uint8_t *data, const size_t len, const char *what) {
        const std::vector<uint8_t> expected = hex(expectedHex);
        CHECK(expected.size() == len && memcmp(expected.data(), data, len) == 0,
              "%s: got [%s], expected [%s]", what, dump(data, len).c_str(),
              dump(expected.data(), expected.size()).c_str());
    }

    template<uint8_t N>
    void expectBuf(const char *expectedHex, const ByteBuf<N> &buf, const char *what) {
        expectBytes(expectedHex, buf.data, buf.len, what);
    }

    const uint32_t BOARD_ID = 0x12345678;

    // Spec 16.4 roster as wire entries.
    std::vector<uint8_t> roster() {
        std::vector<uint8_t> e(3 * ROSTER_ENTRY_SIZE);
        buildRosterEntry(0x0A0B0C0D, 0xff, 0x00, 0x00, "ANNA", &e[0]);
        buildRosterEntry(0x11223344, 0x00, 0x40, 0xff, "KRYSTIAN", &e[16]);
        buildRosterEntry(0x55667788, 0x00, 0xc0, 0x30, "OLA", &e[32]);
        return e;
    }

    uint32_t rosterVersion3() {
        return rosterVersion(3, roster().data());
    }

    std::vector<Frame> chunks(const uint8_t stateSeq, const WatchState &state, const uint8_t ackSeq,
                              const AckStatus ack) {
        uint8_t body[STATE_HEADER + WatchState::MAX_DATA];
        const uint8_t len = state.build(ackSeq, ack, rosterVersion3(), body);
        std::vector<Frame> out;
        for (uint8_t i = 0; i < chunkCount(len); i++) out.push_back(buildStateChunk(stateSeq, body, len, i));
        return out;
    }

    const uint32_t ANNA = 0x0A0B0C0D;
    const uint32_t KRYSTIAN = 0x11223344;
}

void test_crc32_check_value() {
    const char *text = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32(reinterpret_cast<const uint8_t *>(text), 9));
    TEST_ASSERT_EQUAL_HEX32(0, crc32(nullptr, 0));
    // Chained calls equal one call.
    const uint32_t part = crc32(reinterpret_cast<const uint8_t *>(text), 4);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32(reinterpret_cast<const uint8_t *>(text) + 4, 5, part));
}

void test_advertising() {
    expectBuf("0f ff ff ff 53 43 42 44 00 78 56 34 12 00 00 00 02 01 06",
              buildAdvertising(BOARD_ID, false, 0), "16.1 normal");
    expectBuf("0f ff ff ff 53 43 42 44 00 78 56 34 12 01 af 10 02 01 06",
              buildAdvertising(BOARD_ID, true, 4271), "16.1 pairing");
    // The code field is zero outside the window whatever the caller passes.
    expectBuf("0f ff ff ff 53 43 42 44 00 78 56 34 12 00 00 00 02 01 06",
              buildAdvertising(BOARD_ID, false, 4271), "code ignored when not pairing");
}

void test_scan_response() {
    expectBuf("11 07 35 97 e2 c9 8d b7 b7 93 31 4f 44 2f 01 00 78 0d 0b 09 53 63 6f 72 65 2d 35 36 37 38",
              buildScanResponse(BOARD_ID), "16.1 scan rsp");
    const AdvData hexName = buildScanResponse(0x0000ABCD);
    TEST_ASSERT_EQUAL_MEMORY("Score-ABCD", hexName.data + 20, 10);
}

void test_version() {
    expectBuf("00 30 2e 36 2e 31 38 37", buildVersion("0.6.187"), "16.1 version");
    const Frame longest = buildVersion("0123456789012345678901234");
    TEST_ASSERT_EQUAL_UINT8(MAX_FRAME, longest.len);
    TEST_ASSERT_EQUAL_MEMORY("0123456789012345678", longest.data + 1, 19);
}

void test_auth_frames() {
    const std::vector<uint8_t> nb = hex("a0 a1 a2 a3 a4 a5 a6 a7");
    expectBuf("00 00 a0 a1 a2 a3 a4 a5 a6 a7", buildAuthChallenge(nb.data()), "16.2 challenge");

    const std::vector<uint8_t> proof = hex("00 01 02 b0 b1 b2 b3 b4 b5 b6 b7 4e 47 0a 94 b9 24 ac ae");
    AuthProof p{};
    TEST_ASSERT_TRUE(parseAuthProof(proof.data(), proof.size(), p));
    TEST_ASSERT_EQUAL_UINT8(2, p.slotId);
    expectBytes("b0 b1 b2 b3 b4 b5 b6 b7", p.nw, NONCE_SIZE, "proof Nw");
    expectBytes("4e 47 0a 94 b9 24 ac ae", p.mac, MAC_SIZE, "proof MAC");

    TEST_ASSERT_FALSE_MESSAGE(parseAuthProof(proof.data(), 18, p), "short proof");
    std::vector<uint8_t> foreign = proof;
    foreign[0] = 0x01;
    TEST_ASSERT_FALSE_MESSAGE(parseAuthProof(foreign.data(), foreign.size(), p), "foreign PROTO");
    std::vector<uint8_t> wrongType = proof;
    wrongType[1] = 0x00;
    TEST_ASSERT_FALSE_MESSAGE(parseAuthProof(wrongType.data(), wrongType.size(), p), "wrong type");

    const std::vector<uint8_t> boardMac = hex("18 57 e1 b4 a9 6e bf 5a");
    expectBuf("00 02 00 18 57 e1 b4 a9 6e bf 5a", buildAuthResultOk(boardMac.data()), "16.2 result");
    expectBuf("00 02 01", buildAuthResultFail(AuthStatus::BadProof), "16.2 failure");
    expectBuf("00 02 02", buildAuthResultFail(AuthStatus::UnknownSlot), "unknown slot");
}

void test_pairing_frames() {
    const std::vector<uint8_t> request = hex("00 01 af 10");
    uint16_t code = 0;
    TEST_ASSERT_TRUE(parsePairingRequest(request.data(), request.size(), code));
    TEST_ASSERT_EQUAL_UINT16(4271, code);
    TEST_ASSERT_FALSE_MESSAGE(parsePairingRequest(request.data(), 3, code), "short request");

    const std::vector<uint8_t> key = hex("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f");
    expectBuf("00 02 00 02 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f", buildPairingOk(2, key.data()),
              "16.3 result");
    expectBuf("00 02 02", buildPairingFail(PairingStatus::BadCode), "16.3 wrong code");
    expectBuf("00 02 01", buildPairingFail(PairingStatus::WindowClosed), "window closed");
    expectBuf("00 00", buildPairingNone(), "before any request");

    TEST_ASSERT_EQUAL_UINT16(4271, pairingCodeFrom(14271));
    TEST_ASSERT_EQUAL_UINT16(9999, pairingCodeFrom(9999));
    TEST_ASSERT_EQUAL_UINT16(0, pairingCodeFrom(10000));
}

void test_roster() {
    const std::vector<uint8_t> entries = roster();
    std::vector<uint8_t> input(1, 3);
    input.insert(input.end(), entries.begin(), entries.end());
    expectBytes("03 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00 44 33 22 11 00 40 ff"
                "4b 52 59 53 54 49 41 4e 00 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00",
                input.data(), input.size(), "16.4 rosterVersion input");
    TEST_ASSERT_EQUAL_HEX32(0x150fe75b, rosterVersion(3, entries.data()));

    uint8_t cursor = 0xff;
    const std::vector<uint8_t> write = hex("00 00");
    TEST_ASSERT_TRUE(parseRosterCursor(write.data(), write.size(), cursor));
    TEST_ASSERT_EQUAL_UINT8(0, cursor);
    TEST_ASSERT_FALSE_MESSAGE(parseRosterCursor(write.data(), 1, cursor), "short cursor write");

    expectBuf("00 00 03 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00", buildRosterRead(0, 3, entries.data()),
              "16.4 read 0");
    expectBuf("00 01 03 44 33 22 11 00 40 ff 4b 52 59 53 54 49 41 4e 00", buildRosterRead(1, 3, entries.data()),
              "16.4 read 1");
    expectBuf("00 02 03 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00", buildRosterRead(2, 3, entries.data()),
              "16.4 read 2");
    expectBuf("00 03 03", buildRosterRead(3, 3, entries.data()), "16.4 past the end");
    expectBuf("00 c8 03", buildRosterRead(200, 3, entries.data()), "far past the end");

    // A 9-character name fills the field with no NUL; a longer one is cut.
    uint8_t e[ROSTER_ENTRY_SIZE];
    buildRosterEntry(1, 0, 0, 0, "ABCDEFGHIJK", e);
    TEST_ASSERT_EQUAL_MEMORY("ABCDEFGHI", e + 7, 9);
}

void test_unauthenticated_read() {
    expectBuf("00", buildProtoOnly(), "16.8 unauthenticated read");
}

void test_menu_state() {
    WatchState s;
    const SportId sports[] = {SportId::Padel, SportId::Squash, SportId::Volleyball, SportId::ShortVolleyball};
    s.setMenu(SportId::Padel, sports, 4);
    TEST_ASSERT_EQUAL_UINT8(14, s.bodySize());
    const std::vector<Frame> c = chunks(1, s, 0, AckStatus::Applied);
    TEST_ASSERT_EQUAL(1, c.size());
    expectBuf("00 01 01 01 00 00 00 5b e7 0f 15 04 04 04 01 02 03", c[0], "16.5 MENU");
}

void test_squash_playing_state() {
    WatchState s;
    s.setSport(SportId::Squash);
    WatchState::Playing p{};
    p.leftUid = ANNA;
    p.rightUid = KRYSTIAN;
    p.uncommitted = true;
    p.leftGames = 1;
    p.leftScore = 5;
    p.rightScore = 3;
    s.setPlaying(p);
    TEST_ASSERT_EQUAL_UINT8(21, s.bodySize());

    const std::vector<uint8_t> command = hex("00 08 07 00");
    WatchCommand cmd{};
    TEST_ASSERT_EQUAL(static_cast<int>(CommandParse::Ok),
                      static_cast<int>(parseCommand(command.data(), command.size(), cmd)));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandId::Score), static_cast<int>(cmd.id));
    TEST_ASSERT_EQUAL_UINT8(7, cmd.seq);
    TEST_ASSERT_EQUAL_UINT8(0, cmd.side);

    const std::vector<Frame> c = chunks(0x2a, s, cmd.seq, AckStatus::Applied);
    TEST_ASSERT_EQUAL(2, c.size());
    expectBuf("00 2a 02 13 01 07 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 01", c[0], "16.6 chunk 0");
    expectBuf("00 2a 12 01 00 05 03", c[1], "16.6 chunk 1");
}

void test_padel_playing_state() {
    WatchState s;
    s.setSport(SportId::Padel);
    WatchState::Playing p{};
    p.leftUid = ANNA;
    p.rightUid = KRYSTIAN;
    p.tiebreak = true;
    p.leftGames = 1;
    p.leftScore = 6;
    p.rightScore = 6;
    s.setPadelPlaying(p, 3, 2);
    TEST_ASSERT_EQUAL_UINT8(23, s.bodySize());
    std::vector<Frame> c = chunks(0x2b, s, 12, AckStatus::Applied);
    TEST_ASSERT_EQUAL(2, c.size());
    expectBuf("00 2b 02 13 04 0c 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 02", c[0], "16.7 tiebreak chunk 0");
    expectBuf("00 2b 12 01 00 06 06 03 02", c[1], "16.7 tiebreak chunk 1");

    p = WatchState::Playing{};
    p.leftUid = ANNA;
    p.rightUid = KRYSTIAN;
    p.uncommitted = true;
    p.leftScore = 2;
    p.rightScore = 1;
    s.setPadelPlaying(p, 3, 4);
    c = chunks(0x2c, s, 13, AckStatus::Applied);
    expectBuf("00 2c 02 13 04 0d 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 01", c[0], "16.7 ladder chunk 0");
    expectBuf("00 2c 12 00 00 02 01 03 04", c[1], "16.7 ladder chunk 1");
}

void test_other_screens() {
    WatchState s;
    uint8_t body[STATE_HEADER + WatchState::MAX_DATA];

    TEST_ASSERT_EQUAL(static_cast<int>(ScreenId::Booting), static_cast<int>(s.getScreen()));
    TEST_ASSERT_EQUAL_UINT8(8, s.build(0, AckStatus::Applied, 0x11223344, body));
    expectBytes("00 00 00 00 44 33 22 11", body, 8, "BOOTING");

    s.setBusy(ScreenId::Config);
    TEST_ASSERT_EQUAL_UINT8(8, s.bodySize());

    s.setSport(SportId::Volleyball);
    s.setChoosePlayers(0x80000005, 0);
    TEST_ASSERT_EQUAL_UINT8(16, s.build(9, AckStatus::Invalid, 0, body));
    expectBytes("10 02 09 02 00 00 00 00 05 00 00 80 00 00 00 00", body, 16, "CHOOSE_PLAYERS on START");

    s.setMatchStart(0x3, ANNA, KRYSTIAN);
    TEST_ASSERT_EQUAL_UINT8(20, s.build(0, AckStatus::Applied, 0, body));
    expectBytes("11 02 00 00 00 00 00 00 03 00 00 00 0d 0c 0b 0a 44 33 22 11", body, 20, "MATCH_START");

    s.setIntro(ANNA, KRYSTIAN);
    TEST_ASSERT_EQUAL_UINT8(16, s.build(0, AckStatus::Applied, 0, body));
    expectBytes("12 02 00 00 00 00 00 00 0d 0c 0b 0a 44 33 22 11", body, 16, "INTRO");

    WatchState::Result r{};
    r.leftUid = ANNA;
    r.rightUid = KRYSTIAN;
    r.winnerSide = 1;
    r.leftScore = 0;
    r.rightScore = 25;
    r.leftGames = 0;
    r.rightGames = 2;
    r.bajgiel = true;
    s.setCelebration(r);
    TEST_ASSERT_EQUAL_UINT8(22, s.build(0, AckStatus::Applied, 0, body));
    expectBytes("14 02 00 00 00 00 00 00 0d 0c 0b 0a 44 33 22 11 01 00 19 00 02 01", body, 22, "CELEBRATION");
    s.setGameOver(r);
    TEST_ASSERT_EQUAL_UINT8(22, s.build(0, AckStatus::Applied, 0, body));
    TEST_ASSERT_EQUAL_HEX8(0x15, body[0]);

    // Game-ball flags.
    WatchState::Playing p{};
    p.gameBallLeft = true;
    p.gameBallRight = true;
    s.setPlaying(p);
    s.build(0, AckStatus::Applied, 0, body);
    TEST_ASSERT_EQUAL_HEX8(0x0c, body[16]);
}

void test_state_change_detection() {
    WatchState a;
    WatchState b;
    TEST_ASSERT_TRUE(a == b);
    a.setIntro(ANNA, KRYSTIAN);
    TEST_ASSERT_TRUE(a != b);
    b.setIntro(ANNA, KRYSTIAN);
    TEST_ASSERT_TRUE(a == b);
    b.setSport(SportId::Squash);
    TEST_ASSERT_TRUE_MESSAGE(a != b, "sport is part of the change");
    a.setSport(SportId::Squash);
    a.setIntro(ANNA, 1);
    TEST_ASSERT_TRUE_MESSAGE(a != b, "data is part of the change");

    // Stale bytes past the new length never matter.
    a.setMatchStart(1, 2, 3);
    a.setBusy(ScreenId::Profile);
    b.setBusy(ScreenId::Profile);
    TEST_ASSERT_TRUE(a == b);
}

void test_menu_clamped_to_buffer() {
    SportId many[40];
    for (uint8_t i = 0; i < 40; i++) many[i] = SportId::Squash;
    WatchState s;
    s.setMenu(SportId::None, many, 40);
    TEST_ASSERT_EQUAL_UINT8(STATE_HEADER + WatchState::MAX_DATA, s.bodySize());
    uint8_t body[STATE_HEADER + WatchState::MAX_DATA];
    s.build(0, AckStatus::Applied, 0, body);
    TEST_ASSERT_EQUAL_UINT8(WatchState::MAX_DATA - 2, body[9]);
}

void test_chunk_boundaries() {
    uint8_t body[300];
    for (int i = 0; i < 300; i++) body[i] = static_cast<uint8_t>(i);

    const size_t sizes[] = {1, 17, 18, 34, 35, 255};
    const uint8_t counts[] = {1, 1, 2, 2, 3, 15};
    for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        const size_t len = sizes[k];
        TEST_ASSERT_EQUAL_UINT8(counts[k], chunkCount(len));
        std::vector<uint8_t> joined;
        for (uint8_t i = 0; i < counts[k]; i++) {
            const Frame f = buildStateChunk(5, body, len, i);
            CHECK(f.len <= MAX_FRAME && f.len > CHUNK_HEADER, "len %u chunk %u size %u", (unsigned) len, i, f.len);
            CHECK(f.data[0] == PROTO && f.data[1] == 5, "len %u chunk %u header", (unsigned) len, i);
            CHECK(f.data[2] == (i << 4 | counts[k]), "len %u chunk %u index/count 0x%02x", (unsigned) len, i,
                  f.data[2]);
            if (i + 1 < counts[k]) CHECK(f.len == MAX_FRAME, "len %u chunk %u not full", (unsigned) len, i);
            joined.insert(joined.end(), f.data + CHUNK_HEADER, f.data + f.len);
        }
        CHECK(joined.size() == len && memcmp(joined.data(), body, len) == 0, "len %u reassembly", (unsigned) len);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, buildStateChunk(5, body, len, counts[k]).len, "index past count");
    }

    // Last chunk sizes at the edges.
    TEST_ASSERT_EQUAL_UINT8(3 + 1, buildStateChunk(0, body, 18, 1).len);
    TEST_ASSERT_EQUAL_UINT8(3 + 17, buildStateChunk(0, body, 34, 1).len);
    TEST_ASSERT_EQUAL_UINT8(3 + 1, buildStateChunk(0, body, 35, 2).len);
    TEST_ASSERT_EQUAL_HEX8(0xEF, buildStateChunk(0, body, 255, 14).data[2]);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, chunkCount(0), "empty body");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, chunkCount(256), "over 15 chunks");
    TEST_ASSERT_EQUAL_UINT8(0, buildStateChunk(0, body, 256, 0).len);
}

void test_command_sizes() {
    struct Case {
        uint8_t id;
        uint8_t size;
    };
    const Case cases[] = {
        {0x01, 4}, {0x02, 3}, {0x03, 7}, {0x04, 3}, {0x05, 11}, {0x06, 3},
        {0x07, 3}, {0x08, 4}, {0x09, 4}, {0x0A, 3}, {0x0B, 3}, {0x0D, 3}, {0x0E, 7},
    };
    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        uint8_t frame[MAX_FRAME] = {PROTO, cases[k].id, 42};
        WatchCommand cmd{};

        for (uint8_t len = 3; len < cases[k].size; len++) {
            const CommandParse r = parseCommand(frame, len, cmd);
            CHECK(r == CommandParse::Malformed, "id 0x%02x len %u should be malformed", cases[k].id, len);
            CHECK(cmd.seq == 42, "id 0x%02x malformed keeps seq", cases[k].id);
            CHECK(ackFor(r) == AckStatus::Malformed, "id 0x%02x ack", cases[k].id);
        }

        CHECK(parseCommand(frame, cases[k].size, cmd) == CommandParse::Ok, "id 0x%02x exact", cases[k].id);
        CHECK(static_cast<uint8_t>(cmd.id) == cases[k].id && cmd.seq == 42, "id 0x%02x fields", cases[k].id);
        CHECK(ackFor(CommandParse::Ok) == AckStatus::Applied, "ok ack");
        // Spec 2: trailing bytes are ignored.
        CHECK(parseCommand(frame, MAX_FRAME, cmd) == CommandParse::Ok, "id 0x%02x trailing", cases[k].id);
    }
}

void test_command_unknown_and_dropped() {
    WatchCommand cmd{};
    const uint8_t unknownIds[] = {0x00, 0x0C, 0x0F, 0x7F, 0xFF};
    for (size_t k = 0; k < sizeof(unknownIds); k++) {
        const uint8_t frame[] = {PROTO, unknownIds[k], 9, 0, 0, 0, 0, 0, 0, 0, 0};
        const CommandParse r = parseCommand(frame, sizeof(frame), cmd);
        CHECK(r == CommandParse::UnknownCmd, "id 0x%02x should be unknown", unknownIds[k]);
        CHECK(cmd.seq == 9 && ackFor(r) == AckStatus::UnknownCmd, "id 0x%02x ack", unknownIds[k]);
    }
    // 0x0C (dropped END_MATCH) is unknown even at its old size.
    const uint8_t endMatch[] = {PROTO, 0x0C, 3};
    TEST_ASSERT_TRUE(parseCommand(endMatch, 3, cmd) == CommandParse::UnknownCmd);

    const uint8_t foreign[] = {0x01, 0x02, 5};
    TEST_ASSERT_TRUE_MESSAGE(parseCommand(foreign, 3, cmd) == CommandParse::Drop, "foreign PROTO");
    const uint8_t noSeq[] = {PROTO, 0x02};
    TEST_ASSERT_TRUE_MESSAGE(parseCommand(noSeq, 2, cmd) == CommandParse::Drop, "no seq");
    TEST_ASSERT_TRUE_MESSAGE(parseCommand(noSeq, 0, cmd) == CommandParse::Drop, "empty");
}

void test_command_arguments() {
    WatchCommand cmd{};
    const std::vector<uint8_t> setPair = hex("00 05 11 0d 0c 0b 0a 44 33 22 11");
    TEST_ASSERT_TRUE(parseCommand(setPair.data(), setPair.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_EQUAL_HEX32(ANNA, cmd.leftUid);
    TEST_ASSERT_EQUAL_HEX32(KRYSTIAN, cmd.rightUid);
    TEST_ASSERT_EQUAL_UINT8(0x11, cmd.seq);

    const std::vector<uint8_t> toggle = hex("00 03 01 88 77 66 55");
    TEST_ASSERT_TRUE(parseCommand(toggle.data(), toggle.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_EQUAL_HEX32(0x55667788, cmd.uid);

    const std::vector<uint8_t> sport = hex("00 01 02 04");
    TEST_ASSERT_TRUE(parseCommand(sport.data(), sport.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SportId::Padel), cmd.sport);

    const std::vector<uint8_t> undo = hex("00 09 03 01");
    TEST_ASSERT_TRUE(parseCommand(undo.data(), undo.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_TRUE(cmd.id == CommandId::Undo && cmd.side == 1 && cmd.hasValidSide());

    // Side is range-checked by the view (-> INVALID), not by the parser.
    const std::vector<uint8_t> badSide = hex("00 08 04 02");
    TEST_ASSERT_TRUE(parseCommand(badSide.data(), badSide.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_FALSE(cmd.hasValidSide());

    // A previous parse never leaks into the next.
    const std::vector<uint8_t> sync = hex("00 0d 05");
    TEST_ASSERT_TRUE(parseCommand(sync.data(), sync.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_TRUE(cmd.side == 0 && cmd.uid == 0 && cmd.leftUid == 0 && cmd.target == 0);
}

void test_focus() {
    WatchCommand cmd{};

    // 16.9 MENU: focus squash, the state echoes the cursor.
    const std::vector<uint8_t> menuFocus = hex("00 0e 03 01 00 00 00");
    TEST_ASSERT_TRUE(parseCommand(menuFocus.data(), menuFocus.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_TRUE(cmd.id == CommandId::Focus);
    TEST_ASSERT_EQUAL_UINT8(3, cmd.seq);
    TEST_ASSERT_EQUAL_HEX32(0x01, cmd.target);
    TEST_ASSERT_EQUAL_HEX32(0, cmd.uid);

    WatchState s;
    const SportId sports[] = {SportId::Padel, SportId::Squash, SportId::Volleyball, SportId::ShortVolleyball};
    s.setMenu(SportId::Squash, sports, 4);
    std::vector<Frame> c = chunks(2, s, cmd.seq, AckStatus::Applied);
    TEST_ASSERT_EQUAL(1, c.size());
    expectBuf("00 02 01 01 00 03 00 5b e7 0f 15 01 04 04 01 02 03", c[0], "16.9 MENU");

    // 16.9 CHOOSE_PLAYERS: focus KRYSTIAN with ANNA and OLA selected.
    const std::vector<uint8_t> playerFocus = hex("00 0e 04 44 33 22 11");
    TEST_ASSERT_TRUE(parseCommand(playerFocus.data(), playerFocus.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_EQUAL_HEX32(KRYSTIAN, cmd.target);

    s.setSport(SportId::Squash);
    s.setChoosePlayers(0x5, cmd.target);
    TEST_ASSERT_EQUAL_UINT8(16, s.bodySize());
    c = chunks(0x10, s, cmd.seq, AckStatus::Applied);
    TEST_ASSERT_EQUAL(1, c.size());
    expectBuf("00 10 01 10 01 04 00 5b e7 0f 15 05 00 00 00 44 33 22 11", c[0], "16.9 CHOOSE_PLAYERS");

    const std::vector<uint8_t> startFocus = hex("00 0e 05 00 00 00 00");
    TEST_ASSERT_TRUE(parseCommand(startFocus.data(), startFocus.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_EQUAL_HEX32(0, cmd.target);

    // An unknown uid parses; the view answers INVALID.
    const std::vector<uint8_t> nobody = hex("00 0e 06 78 56 34 12");
    TEST_ASSERT_TRUE(parseCommand(nobody.data(), nobody.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, cmd.target);

    const std::vector<uint8_t> shortFocus = hex("00 0e 07 01");
    const CommandParse r = parseCommand(shortFocus.data(), shortFocus.size(), cmd);
    TEST_ASSERT_TRUE(r == CommandParse::Malformed);
    TEST_ASSERT_TRUE(cmd.seq == 7 && ackFor(r) == AckStatus::Malformed);
}

void test_service_uuid_bytes() {
    uint8_t le[16];
    serviceUuidLe(le);
    expectBytes("35 97 e2 c9 8d b7 b7 93 31 4f 44 2f 01 00 78 0d", le, 16, "service UUID LE");
    TEST_ASSERT_EQUAL_STRING("0d780001-2f44-4f31-93b7-b78dc9e29735", SERVICE_UUID);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc32_check_value);
    RUN_TEST(test_advertising);
    RUN_TEST(test_scan_response);
    RUN_TEST(test_version);
    RUN_TEST(test_auth_frames);
    RUN_TEST(test_pairing_frames);
    RUN_TEST(test_roster);
    RUN_TEST(test_unauthenticated_read);
    RUN_TEST(test_menu_state);
    RUN_TEST(test_squash_playing_state);
    RUN_TEST(test_padel_playing_state);
    RUN_TEST(test_other_screens);
    RUN_TEST(test_state_change_detection);
    RUN_TEST(test_menu_clamped_to_buffer);
    RUN_TEST(test_chunk_boundaries);
    RUN_TEST(test_command_sizes);
    RUN_TEST(test_command_unknown_and_dropped);
    RUN_TEST(test_command_arguments);
    RUN_TEST(test_focus);
    RUN_TEST(test_service_uuid_bytes);
    return UNITY_END();
}
