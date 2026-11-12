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
    // spec: advertising/adv_normal
    expectBuf("0f ff ff ff 53 43 42 44 01 78 56 34 12 00 00 00 02 01 06",
              buildAdvertising(BOARD_ID, false, 0), "16.1 normal");
    // spec: advertising/adv_pairing
    expectBuf("0f ff ff ff 53 43 42 44 01 78 56 34 12 01 af 10 02 01 06",
              buildAdvertising(BOARD_ID, true, 4271), "16.1 pairing");
    // The code field is zero outside the window whatever the caller passes.
    expectBuf("0f ff ff ff 53 43 42 44 01 78 56 34 12 00 00 00 02 01 06",
              buildAdvertising(BOARD_ID, false, 4271), "code ignored when not pairing");
}

void test_scan_response() {
    // spec: advertising/scan_rsp
    expectBuf("11 07 35 97 e2 c9 8d b7 b7 93 31 4f 44 2f 01 00 78 0d 0b 09 53 63 6f 72 65 2d 35 36 37 38",
              buildScanResponse(BOARD_ID), "16.1 scan rsp");
    const AdvData hexName = buildScanResponse(0x0000ABCD);
    TEST_ASSERT_EQUAL_MEMORY("Score-ABCD", hexName.data + 20, 10);
}

void test_version() {
    // spec: advertising/version
    expectBuf("01 30 2e 36 2e 31 38 37", buildVersion("0.6.187"), "16.1 version");
    const Frame longest = buildVersion("0123456789012345678901234");
    TEST_ASSERT_EQUAL_UINT8(MAX_FRAME, longest.len);
    TEST_ASSERT_EQUAL_MEMORY("0123456789012345678", longest.data + 1, 19);
}

void test_auth_frames() {
    const std::vector<uint8_t> nb = hex("a0 a1 a2 a3 a4 a5 a6 a7");
    // spec: auth/challenge
    expectBuf("01 00 a0 a1 a2 a3 a4 a5 a6 a7", buildAuthChallenge(nb.data()), "16.2 challenge");

    // spec: auth/proof
    const std::vector<uint8_t> proof = hex("01 01 02 b0 b1 b2 b3 b4 b5 b6 b7 4e 47 0a 94 b9 24 ac ae");
    AuthProof p{};
    TEST_ASSERT_TRUE(parseAuthProof(proof.data(), proof.size(), p));
    TEST_ASSERT_EQUAL_UINT8(2, p.slotId);
    expectBytes("b0 b1 b2 b3 b4 b5 b6 b7", p.nw, NONCE_SIZE, "proof Nw");
    expectBytes("4e 47 0a 94 b9 24 ac ae", p.mac, MAC_SIZE, "proof MAC");

    TEST_ASSERT_FALSE_MESSAGE(parseAuthProof(proof.data(), 18, p), "short proof");
    std::vector<uint8_t> foreign = proof;
    foreign[0] = 0x00;
    TEST_ASSERT_FALSE_MESSAGE(parseAuthProof(foreign.data(), foreign.size(), p), "foreign PROTO");
    std::vector<uint8_t> wrongType = proof;
    wrongType[1] = 0x00;
    TEST_ASSERT_FALSE_MESSAGE(parseAuthProof(wrongType.data(), wrongType.size(), p), "wrong type");

    const std::vector<uint8_t> boardMac = hex("18 57 e1 b4 a9 6e bf 5a");
    // spec: auth/result_ok
    expectBuf("01 02 00 18 57 e1 b4 a9 6e bf 5a", buildAuthResultOk(boardMac.data()), "16.2 result");
    // spec: auth/result_bad_proof
    expectBuf("01 02 01", buildAuthResultFail(AuthStatus::BadProof), "16.2 failure");
    expectBuf("01 02 02", buildAuthResultFail(AuthStatus::UnknownSlot), "unknown slot");
}

void test_pairing_frames() {
    // spec: pairing/request
    const std::vector<uint8_t> request = hex("01 01 af 10");
    uint16_t code = 0;
    TEST_ASSERT_TRUE(parsePairingRequest(request.data(), request.size(), code));
    TEST_ASSERT_EQUAL_UINT16(4271, code);
    TEST_ASSERT_FALSE_MESSAGE(parsePairingRequest(request.data(), 3, code), "short request");

    const std::vector<uint8_t> key = hex("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f");
    // spec: pairing/result_ok
    expectBuf("01 02 00 02 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f", buildPairingOk(2, key.data()),
              "16.3 result");
    // spec: pairing/result_bad_code
    expectBuf("01 02 02", buildPairingFail(PairingStatus::BadCode), "16.3 wrong code");
    expectBuf("01 02 01", buildPairingFail(PairingStatus::WindowClosed), "window closed");
    expectBuf("01 00", buildPairingNone(), "before any request");

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
    // spec: roster_read/write
    // spec: roster_read_teams/write
    const std::vector<uint8_t> write = hex("01 00");
    TEST_ASSERT_TRUE(parseRosterCursor(write.data(), write.size(), cursor));
    TEST_ASSERT_EQUAL_UINT8(0, cursor);
    TEST_ASSERT_FALSE_MESSAGE(parseRosterCursor(write.data(), 1, cursor), "short cursor write");

    // spec: roster_read/read_anna
    expectBuf("01 00 03 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00", buildRosterRead(0, 3, entries.data()),
              "16.4 read 0");
    // spec: roster_read/read_krystian
    expectBuf("01 01 03 44 33 22 11 00 40 ff 4b 52 59 53 54 49 41 4e 00", buildRosterRead(1, 3, entries.data()),
              "16.4 read 1");
    // spec: roster_read/read_ola
    expectBuf("01 02 03 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00", buildRosterRead(2, 3, entries.data()),
              "16.4 read 2");
    // spec: roster_read/read_past_end
    expectBuf("01 03 03", buildRosterRead(3, 3, entries.data()), "16.4 past the end");
    expectBuf("01 c8 03", buildRosterRead(200, 3, entries.data()), "far past the end");

    // 16.4 teams roster.
    uint8_t teams[2 * ROSTER_ENTRY_SIZE];
    buildRosterEntry(0x21222324, 0xff, 0x80, 0x00, "ORLY", teams);
    buildRosterEntry(0x31323334, 0x00, 0xff, 0x80, "LISY", teams + 16);
    std::vector<uint8_t> teamInput(1, 2);
    teamInput.insert(teamInput.end(), teams, teams + sizeof(teams));
    expectBytes("02 24 23 22 21 ff 80 00 4f 52 4c 59 00 00 00 00 00 34 33 32 31 00 ff 80"
                "4c 49 53 59 00 00 00 00 00",
                teamInput.data(), teamInput.size(), "16.4 teams rosterVersion input");
    TEST_ASSERT_EQUAL_HEX32(0xdb5f702c, rosterVersion(2, teams));
    // spec: roster_read_teams/read_orly
    expectBuf("01 00 02 24 23 22 21 ff 80 00 4f 52 4c 59 00 00 00 00 00", buildRosterRead(0, 2, teams),
              "16.4 teams read 0");
    // spec: roster_read_teams/read_lisy
    expectBuf("01 01 02 34 33 32 31 00 ff 80 4c 49 53 59 00 00 00 00 00", buildRosterRead(1, 2, teams),
              "16.4 teams read 1");
    // spec: roster_read_teams/read_past_end
    expectBuf("01 02 02", buildRosterRead(2, 2, teams), "16.4 teams past the end");

    // A 9-character name fills the field with no NUL; a longer one is cut.
    uint8_t e[ROSTER_ENTRY_SIZE];
    buildRosterEntry(1, 0, 0, 0, "ABCDEFGHIJK", e);
    TEST_ASSERT_EQUAL_MEMORY("ABCDEFGHI", e + 7, 9);
}

void test_unauthenticated_read() {
    // spec: board_version/unauth_read
    // spec: results/unauth_read
    expectBuf("01", buildProtoOnly(), "16.8 unauthenticated read");
}

void test_menu_state() {
    WatchState s;
    const SportId sports[] = {SportId::Padel, SportId::Squash, SportId::Volleyball, SportId::ShortVolleyball};
    s.setMenu(SportId::Padel, sports, 4);
    TEST_ASSERT_EQUAL_UINT8(14, s.bodySize());
    const std::vector<Frame> c = chunks(1, s, 0, AckStatus::Applied);
    TEST_ASSERT_EQUAL(1, c.size());
    // spec: menu_state/menu
    expectBuf("01 01 01 01 00 00 00 5b e7 0f 15 04 04 04 01 02 03", c[0], "16.5 MENU");
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
    p.lastKnown = true;   // the left's point is the newest
    s.setPlaying(p);
    TEST_ASSERT_EQUAL_UINT8(21, s.bodySize());

    // spec: score_confirm/score
    const std::vector<uint8_t> command = hex("01 08 07 00");
    WatchCommand cmd{};
    TEST_ASSERT_EQUAL(static_cast<int>(CommandParse::Ok),
                      static_cast<int>(parseCommand(command.data(), command.size(), cmd)));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandId::Score), static_cast<int>(cmd.id));
    TEST_ASSERT_EQUAL_UINT8(7, cmd.seq);
    TEST_ASSERT_EQUAL_UINT8(0, cmd.side);

    const std::vector<Frame> c = chunks(0x2a, s, cmd.seq, AckStatus::Applied);
    TEST_ASSERT_EQUAL(2, c.size());
    // spec: score_confirm/state
    expectBuf("01 2a 02 13 01 07 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 11", c[0], "16.6 chunk 0");
    expectBuf("01 2a 12 01 00 05 03", c[1], "16.6 chunk 1");
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
    p.lastKnown = true;   // the left won the last rally
    s.setPadelPlaying(p, 3, 2);
    TEST_ASSERT_EQUAL_UINT8(23, s.bodySize());
    std::vector<Frame> c = chunks(0x2b, s, 12, AckStatus::Applied);
    TEST_ASSERT_EQUAL(2, c.size());
    // spec: padel_playing/tiebreak
    expectBuf("01 2b 02 13 04 0c 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 12", c[0], "16.7 tiebreak chunk 0");
    expectBuf("01 2b 12 01 00 06 06 03 02", c[1], "16.7 tiebreak chunk 1");

    p = WatchState::Playing{};
    p.leftUid = ANNA;
    p.rightUid = KRYSTIAN;
    p.uncommitted = true;
    p.leftScore = 2;
    p.rightScore = 1;
    p.lastKnown = true;
    p.lastRight = true;
    s.setPadelPlaying(p, 3, 4);
    c = chunks(0x2c, s, 13, AckStatus::Applied);
    // spec: padel_playing/ladder
    expectBuf("01 2c 02 13 04 0d 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 31", c[0], "16.7 ladder chunk 0");
    expectBuf("01 2c 12 00 00 02 01 03 04", c[1], "16.7 ladder chunk 1");
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

    s.setMatchStart(0x3, ANNA, KRYSTIAN, false);
    TEST_ASSERT_EQUAL_UINT8(21, s.build(0, AckStatus::Applied, 0, body));
    expectBytes("11 02 00 00 00 00 00 00 03 00 00 00 0d 0c 0b 0a 44 33 22 11 00", body, 21, "MATCH_START");

    s.setMatchStart(0x3, ANNA, KRYSTIAN, true);
    TEST_ASSERT_EQUAL_UINT8(21, s.build(0, AckStatus::Applied, 0, body));
    expectBytes("11 02 00 00 00 00 00 00 03 00 00 00 0d 0c 0b 0a 44 33 22 11 40", body, 21, "MATCH_START swapped");

    s.setIntro(ANNA, KRYSTIAN, false);
    TEST_ASSERT_EQUAL_UINT8(17, s.build(0, AckStatus::Applied, 0, body));
    expectBytes("12 02 00 00 00 00 00 00 0d 0c 0b 0a 44 33 22 11 00", body, 17, "INTRO");

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
    TEST_ASSERT_EQUAL_UINT8(23, s.build(0, AckStatus::Applied, 0, body));
    expectBytes("14 02 00 00 00 00 00 00 0d 0c 0b 0a 44 33 22 11 01 00 19 00 02 01 00", body, 23, "CELEBRATION");
    s.setGameOver(r);
    TEST_ASSERT_EQUAL_UINT8(23, s.build(0, AckStatus::Applied, 0, body));
    TEST_ASSERT_EQUAL_HEX8(0x15, body[0]);

    // Game-ball flags.
    WatchState::Playing p{};
    p.gameBallLeft = true;
    p.gameBallRight = true;
    s.setPlaying(p);
    s.build(0, AckStatus::Applied, 0, body);
    TEST_ASSERT_EQUAL_HEX8(0x0c, body[16]);
}

// Spec 16.11: the pair now sits reversed against its first game.
void test_sides_swapped_state() {
    WatchState s;
    s.setSport(SportId::Squash);
    s.setIntro(ANNA, KRYSTIAN, true);
    TEST_ASSERT_EQUAL_UINT8(17, s.bodySize());
    std::vector<Frame> c = chunks(0x30, s, 0, AckStatus::Applied);
    TEST_ASSERT_EQUAL(1, c.size());
    // spec: sides_swapped/intro
    expectBuf("01 30 01 12 01 00 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 40", c[0], "16.11 INTRO");

    WatchState::Playing p{};
    p.leftUid = ANNA;
    p.rightUid = KRYSTIAN;
    p.rightGames = 1;
    p.leftScore = 2;
    p.rightScore = 1;
    p.sidesSwapped = true;
    p.lastKnown = true;   // the left's point is the newest
    s.setPlaying(p);
    TEST_ASSERT_EQUAL_UINT8(21, s.bodySize());
    c = chunks(0x31, s, 0, AckStatus::Applied);
    TEST_ASSERT_EQUAL(2, c.size());
    // spec: sides_swapped/playing
    expectBuf("01 31 02 13 01 00 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 50", c[0], "16.11 PLAYING chunk 0");
    expectBuf("01 31 12 00 01 02 01", c[1], "16.11 PLAYING chunk 1");

    // Bit 6 sits beside the other flags.
    p.uncommitted = true;
    p.gameBallLeft = true;
    s.setPlaying(p);
    c = chunks(0x31, s, 0, AckStatus::Applied);
    TEST_ASSERT_EQUAL_HEX8(0x55, c[0].data[19]);
    p.tiebreak = true;
    p.gameBallRight = true;
    p.lastRight = true;
    s.setPlaying(p);
    c = chunks(0x31, s, 0, AckStatus::Applied);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x7f, c[0].data[19], "bit 7 stays reserved 0");

    WatchState::Result r{};
    r.leftUid = ANNA;
    r.rightUid = KRYSTIAN;
    r.winnerSide = 0;
    r.leftScore = 11;
    r.rightScore = 7;
    r.leftGames = 1;
    r.rightGames = 1;
    r.sidesSwapped = true;
    s.setGameOver(r);
    TEST_ASSERT_EQUAL_UINT8(23, s.bodySize());
    c = chunks(0x32, s, 0, AckStatus::Applied);
    TEST_ASSERT_EQUAL(2, c.size());
    // spec: sides_swapped/game_over
    expectBuf("01 32 02 15 01 00 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 00", c[0], "16.11 GAME_OVER chunk 0");
    expectBuf("01 32 12 0b 07 01 01 00 40", c[1], "16.11 GAME_OVER chunk 1");

    // spec: sides_swapped/match_start
    s.setSport(SportId::Squash);
    s.setMatchStart(0x3, ANNA, KRYSTIAN, true);
    TEST_ASSERT_EQUAL_UINT8(21, s.bodySize());
    c = chunks(0x33, s, 0, AckStatus::Applied);
    TEST_ASSERT_EQUAL(2, c.size());
    expectBuf("01 33 02 11 01 00 00 5b e7 0f 15 03 00 00 00 0d 0c 0b 0a 44", c[0], "16.11 MATCH_START chunk 0");
    expectBuf("01 33 12 33 22 11 40", c[1], "16.11 MATCH_START chunk 1");

    s.setCelebration(r);
    c = chunks(0x33, s, 0, AckStatus::Applied);
    TEST_ASSERT_EQUAL(2, c.size());
    expectBuf("01 33 12 0b 07 01 01 00 40", c[1], "CELEBRATION chunk 1");
}

// Spec 16.12: the newest point's side, as the board's own history has it.
void test_last_point_state() {
    WatchState s;
    s.setSport(SportId::Squash);
    WatchState::Playing p{};
    p.leftUid = ANNA;
    p.rightUid = KRYSTIAN;
    p.uncommitted = true;
    p.rightScore = 1;
    p.lastKnown = true;
    p.lastRight = true;
    s.setPlaying(p);
    std::vector<Frame> c = chunks(0x40, s, 1, AckStatus::Applied);
    TEST_ASSERT_EQUAL(2, c.size());
    // spec: last_known/state
    expectBuf("01 40 02 13 01 01 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 31", c[0], "16.12 chunk 0");
    expectBuf("01 40 12 00 00 00 01", c[1], "16.12 chunk 1");

    // The flags of the walk in 16.12.
    struct Step { bool uncommitted, known, right; uint8_t flags; };
    const Step steps[] = {
        {false, false, false, 0x00},   // 0:0
        {false, true, true, 0x30},     // committed
        {true, true, false, 0x11},     // left scores
        {false, true, true, 0x30},     // undone at once
        {true, false, false, 0x01},    // the committed right point undone
    };
    for (const Step &step : steps) {
        p.uncommitted = step.uncommitted;
        p.lastKnown = step.known;
        p.lastRight = step.right;
        s.setPlaying(p);
        c = chunks(0x41, s, 0, AckStatus::Applied);
        TEST_ASSERT_EQUAL_HEX8(step.flags, c[0].data[19]);
    }

    // LAST_RIGHT is never sent without LAST_KNOWN.
    p.uncommitted = false;
    p.lastKnown = false;
    p.lastRight = true;
    s.setPlaying(p);
    c = chunks(0x42, s, 0, AckStatus::Applied);
    TEST_ASSERT_EQUAL_HEX8(0x00, c[0].data[19]);
}

void test_state_change_detection() {
    WatchState a;
    WatchState b;
    TEST_ASSERT_TRUE(a == b);
    a.setIntro(ANNA, KRYSTIAN, false);
    TEST_ASSERT_TRUE(a != b);
    b.setIntro(ANNA, KRYSTIAN, false);
    TEST_ASSERT_TRUE(a == b);
    b.setSport(SportId::Squash);
    TEST_ASSERT_TRUE_MESSAGE(a != b, "sport is part of the change");
    a.setSport(SportId::Squash);
    a.setIntro(ANNA, 1, false);
    TEST_ASSERT_TRUE_MESSAGE(a != b, "data is part of the change");

    // Stale bytes past the new length never matter.
    a.setMatchStart(1, 2, 3, false);
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
        {0x0F, 13}, {0x10, 11}, {0x11, 13}, {0x12, 12}, {0x13, 9}, {0x14, 8},
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
    const uint8_t unknownIds[] = {0x00, 0x0C, 0x15, 0x7F, 0xFF};
    for (size_t k = 0; k < sizeof(unknownIds); k++) {
        const uint8_t frame[] = {PROTO, unknownIds[k], 9, 0, 0, 0, 0, 0, 0, 0, 0};
        const CommandParse r = parseCommand(frame, sizeof(frame), cmd);
        CHECK(r == CommandParse::UnknownCmd, "id 0x%02x should be unknown", unknownIds[k]);
        CHECK(cmd.seq == 9 && ackFor(r) == AckStatus::UnknownCmd, "id 0x%02x ack", unknownIds[k]);
    }
    // 0x0C (dropped END_MATCH) is unknown even at its old size.
    const uint8_t endMatch[] = {PROTO, 0x0C, 3};
    TEST_ASSERT_TRUE(parseCommand(endMatch, 3, cmd) == CommandParse::UnknownCmd);

    const uint8_t foreign[] = {0x00, 0x02, 5};
    TEST_ASSERT_TRUE_MESSAGE(parseCommand(foreign, 3, cmd) == CommandParse::Drop, "foreign PROTO");
    const uint8_t noSeq[] = {PROTO, 0x02};
    TEST_ASSERT_TRUE_MESSAGE(parseCommand(noSeq, 2, cmd) == CommandParse::Drop, "no seq");
    TEST_ASSERT_TRUE_MESSAGE(parseCommand(noSeq, 0, cmd) == CommandParse::Drop, "empty");
}

void test_command_arguments() {
    WatchCommand cmd{};
    const std::vector<uint8_t> setPair = hex("01 05 11 0d 0c 0b 0a 44 33 22 11");
    TEST_ASSERT_TRUE(parseCommand(setPair.data(), setPair.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_EQUAL_HEX32(ANNA, cmd.leftUid);
    TEST_ASSERT_EQUAL_HEX32(KRYSTIAN, cmd.rightUid);
    TEST_ASSERT_EQUAL_UINT8(0x11, cmd.seq);

    const std::vector<uint8_t> toggle = hex("01 03 01 88 77 66 55");
    TEST_ASSERT_TRUE(parseCommand(toggle.data(), toggle.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_EQUAL_HEX32(0x55667788, cmd.uid);

    const std::vector<uint8_t> sport = hex("01 01 02 04");
    TEST_ASSERT_TRUE(parseCommand(sport.data(), sport.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SportId::Padel), cmd.sport);

    const std::vector<uint8_t> undo = hex("01 09 03 01");
    TEST_ASSERT_TRUE(parseCommand(undo.data(), undo.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_TRUE(cmd.id == CommandId::Undo && cmd.side == 1 && cmd.hasValidSide());

    // Side is range-checked by the view (-> INVALID), not by the parser.
    const std::vector<uint8_t> badSide = hex("01 08 04 02");
    TEST_ASSERT_TRUE(parseCommand(badSide.data(), badSide.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_FALSE(cmd.hasValidSide());

    // spec: last_known/score_right
    const std::vector<uint8_t> scoreRight = hex("01 08 01 01");
    TEST_ASSERT_TRUE(parseCommand(scoreRight.data(), scoreRight.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_TRUE(cmd.id == CommandId::Score && cmd.seq == 1 && cmd.side == 1 && cmd.hasValidSide());

    // A previous parse never leaks into the next.
    const std::vector<uint8_t> sync = hex("01 0d 05");
    TEST_ASSERT_TRUE(parseCommand(sync.data(), sync.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_TRUE(cmd.side == 0 && cmd.uid == 0 && cmd.leftUid == 0 && cmd.target == 0);
}

void test_focus() {
    WatchCommand cmd{};

    // 16.9 MENU: focus squash, the state echoes the cursor.
    // spec: focus/focus_squash
    const std::vector<uint8_t> menuFocus = hex("01 0e 03 01 00 00 00");
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
    // spec: focus/menu_state
    expectBuf("01 02 01 01 00 03 00 5b e7 0f 15 01 04 04 01 02 03", c[0], "16.9 MENU");

    // 16.9 CHOOSE_PLAYERS: focus KRYSTIAN with ANNA and OLA selected.
    // spec: focus/focus_krystian
    const std::vector<uint8_t> playerFocus = hex("01 0e 04 44 33 22 11");
    TEST_ASSERT_TRUE(parseCommand(playerFocus.data(), playerFocus.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_EQUAL_HEX32(KRYSTIAN, cmd.target);

    s.setSport(SportId::Squash);
    s.setChoosePlayers(0x5, cmd.target);
    TEST_ASSERT_EQUAL_UINT8(16, s.bodySize());
    c = chunks(0x10, s, cmd.seq, AckStatus::Applied);
    TEST_ASSERT_EQUAL(1, c.size());
    // spec: focus/choose_players_state
    expectBuf("01 10 01 10 01 04 00 5b e7 0f 15 05 00 00 00 44 33 22 11", c[0], "16.9 CHOOSE_PLAYERS");

    // spec: focus/focus_start
    const std::vector<uint8_t> startFocus = hex("01 0e 05 00 00 00 00");
    TEST_ASSERT_TRUE(parseCommand(startFocus.data(), startFocus.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_EQUAL_HEX32(0, cmd.target);

    // An unknown uid parses; the view answers INVALID.
    // spec: focus/focus_unknown_uid
    const std::vector<uint8_t> nobody = hex("01 0e 06 78 56 34 12");
    TEST_ASSERT_TRUE(parseCommand(nobody.data(), nobody.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, cmd.target);

    // spec: focus/focus_malformed
    const std::vector<uint8_t> shortFocus = hex("01 0e 07 01");
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

// Spec 16.10.
void test_heart_frames() {
    uint8_t bpm = 0xff;
    // spec: heart/write_a
    const std::vector<uint8_t> write = hex("01 92");
    TEST_ASSERT_TRUE(parseHeartWrite(write.data(), write.size(), bpm));
    TEST_ASSERT_EQUAL_UINT8(146, bpm);
    // spec: heart/write_c
    const std::vector<uint8_t> writeC = hex("01 7c");
    TEST_ASSERT_TRUE(parseHeartWrite(writeC.data(), writeC.size(), bpm));
    TEST_ASSERT_EQUAL_UINT8(124, bpm);
    // spec: heart/write_c_clear
    const std::vector<uint8_t> clear = hex("01 00");
    TEST_ASSERT_TRUE(parseHeartWrite(clear.data(), clear.size(), bpm));
    TEST_ASSERT_EQUAL_UINT8(0, bpm);
    // spec: heart/discard_short
    const std::vector<uint8_t> oneByte = hex("01");
    TEST_ASSERT_FALSE_MESSAGE(parseHeartWrite(oneByte.data(), oneByte.size(), bpm), "1-byte write");
    TEST_ASSERT_FALSE_MESSAGE(parseHeartWrite(write.data(), 1, bpm), "short");
    // spec: heart/discard_v0
    const std::vector<uint8_t> foreign = hex("00 92");
    TEST_ASSERT_FALSE_MESSAGE(parseHeartWrite(foreign.data(), foreign.size(), bpm), "foreign PROTO");
    const std::vector<uint8_t> longer = hex("01 92 ff");
    TEST_ASSERT_TRUE_MESSAGE(parseHeartWrite(longer.data(), longer.size(), bpm) && bpm == 146, "trailing byte");

    // spec: heart/notify_a
    const uint8_t oneA[HEART_LIST] = {0x7c, 0, 0};
    expectBuf("01 7c 00 00", buildHeart(oneA), "16.10 A's notify");
    // spec: heart/notify_b
    const uint8_t both[HEART_LIST] = {0x92, 0x7c, 0};
    expectBuf("01 92 7c 00", buildHeart(both), "16.10 B's notify");
    // spec: heart/notify_c
    const uint8_t one[HEART_LIST] = {0x92, 0, 0};
    expectBuf("01 92 00 00", buildHeart(one), "16.10 C's notify");
    // spec: heart/notify_a_empty
    const uint8_t none[HEART_LIST] = {0, 0, 0};
    expectBuf("01 00 00 00", buildHeart(none), "16.10 none");
    TEST_ASSERT_EQUAL_STRING("0d780008-2f44-4f31-93b7-b78dc9e29735", HEART_UUID);
}

// Spec 16.13.
void test_results_frames() {
    uint8_t entries[3 * RESULT_ENTRY_SIZE];
    buildResultEntry(0x0A0B0C0D, 0x11223344, 11, 7, 1, entries);
    buildResultEntry(0x11223344, 0x55667788, 11, 4, 1, entries + RESULT_ENTRY_SIZE);
    buildResultEntry(0x0A0B0C0D, 0x11223344, 9, 11, 2, entries + 2 * RESULT_ENTRY_SIZE);
    const uint32_t tid = 0xC0FFEE01;
    // spec: results/read_1
    expectBuf("01 00 00 03 00 01 ee ff c0 0d 0c 0b 0a 44 33 22 11 0b 07 01", buildResultsRead(0, 3, tid, 0, entries), "game 0");
    // spec: results/read_2
    expectBuf("01 01 00 03 00 01 ee ff c0 44 33 22 11 88 77 66 55 0b 04 01", buildResultsRead(1, 3, tid, 0, entries), "game 1");
    // spec: results/read_3
    expectBuf("01 02 00 03 00 01 ee ff c0 0d 0c 0b 0a 44 33 22 11 09 0b 02", buildResultsRead(2, 3, tid, 0, entries), "game 2");
    // spec: results/read_past_end
    expectBuf("01 03 00 03 00 01 ee ff c0 00 00", buildResultsRead(3, 3, tid, 0, entries), "past the end");
    // spec: results/read_no_tournament
    expectBuf("01 03 00 00 00 00 00 00 00 00 00", buildResultsRead(3, 0, 0, 0, nullptr), "no tournament");
    TEST_ASSERT_EQUAL_UINT8(RESULTS_END_FRAME, buildResultsRead(3, 3, tid, 0, entries).len);

    uint16_t index = 0xffff;
    // spec: results/write
    const std::vector<uint8_t> writeZero = hex("01 00 00");
    TEST_ASSERT_TRUE(parseResultsCursor(writeZero.data(), writeZero.size(), index));
    TEST_ASSERT_EQUAL_UINT16(0, index);
    const std::vector<uint8_t> write = hex("01 03 00");
    TEST_ASSERT_TRUE(parseResultsCursor(write.data(), write.size(), index));
    TEST_ASSERT_EQUAL_UINT16(3, index);
    const std::vector<uint8_t> wide = hex("01 34 12 ff");
    TEST_ASSERT_TRUE_MESSAGE(parseResultsCursor(wide.data(), wide.size(), index) && index == 0x1234, "u16, trailing byte");
    // spec: results/discard_short
    const std::vector<uint8_t> shortWrite = hex("01 02");
    TEST_ASSERT_FALSE_MESSAGE(parseResultsCursor(shortWrite.data(), shortWrite.size(), index), "short");
    // spec: results/discard_v0
    const std::vector<uint8_t> foreign = hex("00 00 00");
    TEST_ASSERT_FALSE_MESSAGE(parseResultsCursor(foreign.data(), foreign.size(), index), "foreign PROTO");
    TEST_ASSERT_EQUAL_STRING("0d780009-2f44-4f31-93b7-b78dc9e29735", RESULTS_UUID);
}

// Spec 16.14: ADD / REMOVE_OFFLINE_GAME, the OFFLINE turn bit and rev on the end frame.
void test_offline_games() {
    WatchCommand cmd{};
    WatchState s;
    s.setSport(SportId::Squash);
    s.setMatchStart(0x3, ANNA, KRYSTIAN, false);

    // spec: offline_games/add
    const std::vector<uint8_t> add = hex("01 0f 05 0d 0c 0b 0a 44 33 22 11 0b 07");
    TEST_ASSERT_TRUE(parseCommand(add.data(), add.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_TRUE(cmd.id == CommandId::AddOfflineGame && cmd.seq == 5);
    TEST_ASSERT_EQUAL_HEX32(ANNA, cmd.uidA);
    TEST_ASSERT_EQUAL_HEX32(KRYSTIAN, cmd.uidB);
    TEST_ASSERT_TRUE(cmd.scoreA == 11 && cmd.scoreB == 7);
    std::vector<Frame> c = chunks(0x50, s, cmd.seq, AckStatus::Applied);
    TEST_ASSERT_EQUAL(2, c.size());
    // spec: offline_games/add_ack
    expectBuf("01 50 02 11 01 05 00 5b e7 0f 15 03 00 00 00 0d 0c 0b 0a 44", c[0], "16.14 add ack chunk 0");
    expectBuf("01 50 12 33 22 11 00", c[1], "16.14 add ack chunk 1");

    // spec: offline_games/add_illegal
    const std::vector<uint8_t> illegal = hex("01 0f 06 0d 0c 0b 0a 44 33 22 11 0f 03");
    TEST_ASSERT_TRUE(parseCommand(illegal.data(), illegal.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_TRUE(cmd.seq == 6 && cmd.scoreA == 15 && cmd.scoreB == 3);
    c = chunks(0x51, s, cmd.seq, AckStatus::Invalid);
    TEST_ASSERT_EQUAL(2, c.size());
    // spec: offline_games/add_illegal_ack
    expectBuf("01 51 02 11 01 06 02 5b e7 0f 15 03 00 00 00 0d 0c 0b 0a 44", c[0], "16.14 illegal ack chunk 0");
    expectBuf("01 51 12 33 22 11 00", c[1], "16.14 illegal ack chunk 1");

    uint8_t entry[RESULT_ENTRY_SIZE];
    buildResultEntry(ANNA, KRYSTIAN, 11, 7, 1 | TURN_OFFLINE, entry);
    // spec: offline_games/read_offline
    expectBuf("01 00 00 01 00 01 ee ff c0 0d 0c 0b 0a 44 33 22 11 0b 07 81", buildResultsRead(0, 1, 0xC0FFEE01, 0, entry),
              "16.14 offline game");

    // spec: offline_games/remove
    const std::vector<uint8_t> remove = hex("01 10 07 01 ee ff c0 00 00 00 00");
    TEST_ASSERT_TRUE(parseCommand(remove.data(), remove.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_TRUE(cmd.id == CommandId::RemoveOfflineGame && cmd.seq == 7);
    TEST_ASSERT_EQUAL_HEX32(0xC0FFEE01, cmd.tournamentId);
    TEST_ASSERT_TRUE(cmd.rev == 0 && cmd.index == 0);
    c = chunks(0x52, s, cmd.seq, AckStatus::Applied);
    TEST_ASSERT_EQUAL(2, c.size());
    // spec: offline_games/remove_ack
    expectBuf("01 52 02 11 01 07 00 5b e7 0f 15 03 00 00 00 0d 0c 0b 0a 44", c[0], "16.14 remove ack chunk 0");
    expectBuf("01 52 12 33 22 11 00", c[1], "16.14 remove ack chunk 1");

    // spec: offline_games/read_end_rev
    expectBuf("01 00 00 00 00 01 ee ff c0 01 00", buildResultsRead(0, 0, 0xC0FFEE01, 1, nullptr), "16.14 end with rev");

    // rev and index are u16 little-endian.
    const std::vector<uint8_t> wide = hex("01 10 08 01 ee ff c0 34 12 02 01");
    TEST_ASSERT_TRUE(parseCommand(wide.data(), wide.size(), cmd) == CommandParse::Ok);
    TEST_ASSERT_TRUE(cmd.rev == 0x1234 && cmd.index == 0x0102);

    // One byte short of each layout: MALFORMED with the seq.
    TEST_ASSERT_TRUE(parseCommand(add.data(), add.size() - 1, cmd) == CommandParse::Malformed && cmd.seq == 5);
    TEST_ASSERT_TRUE(parseCommand(remove.data(), remove.size() - 1, cmd) == CommandParse::Malformed && cmd.seq == 7);
}

namespace {
    const uint32_t OLA = 0x55667788;
    const uint32_t TABLET_TID = 0x7AB1E001;
    // 16.15: rosterVersion of ANNA + KRYSTIAN, the first push.
    const uint32_t TWO_ENTRY_CRC = 0xC694F1D4;

    std::vector<Frame> chunksAt(const uint8_t stateSeq, const WatchState &state, const uint8_t ackSeq,
                                const AckStatus ack, const uint32_t version) {
        uint8_t body[STATE_HEADER + WatchState::MAX_DATA];
        const uint8_t len = state.build(ackSeq, ack, version, body);
        std::vector<Frame> out;
        for (uint8_t i = 0; i < chunkCount(len); i++) out.push_back(buildStateChunk(stateSeq, body, len, i));
        return out;
    }

    WatchCommand parsed(const char *frameHex) {
        const std::vector<uint8_t> f = hex(frameHex);
        WatchCommand cmd{};
        TEST_ASSERT_TRUE(parseCommand(f.data(), f.size(), cmd) == CommandParse::Ok);
        return cmd;
    }

    void expectChunks(const std::vector<Frame> &c, const char *first, const char *second, const char *what) {
        TEST_ASSERT_EQUAL(2, c.size());
        expectBuf(first, c[0], what);
        expectBuf(second, c[1], what);
    }

    WatchState tabletMatchStart(const uint32_t selected, const uint32_t right) {
        WatchState s;
        s.setSport(SportId::Squash);
        s.setMatchStart(selected, ANNA, right, false);
        return s;
    }
}

// Spec 16.15: the TOURNAMENT characteristic's frames, the four tablet commands and their acks.
void test_tablet_frames() {
    TEST_ASSERT_EQUAL_STRING("0d78000a-2f44-4f31-93b7-b78dc9e29735", TOURNAMENT_UUID);

    WatchState wait;
    wait.setBusy(ScreenId::TournamentWait);
    const std::vector<Frame> w = chunks(0x60, wait, 0, AckStatus::Applied);
    TEST_ASSERT_EQUAL(1, w.size());
    // spec: tablet_begin/wait_state
    expectBuf("01 60 01 04 00 00 00 5b e7 0f 15", w[0], "TOURNAMENT_WAIT");

    // spec: tablet_begin/status_none
    expectBuf("01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00", buildTournamentStatus(TabletStatus{}),
              "no tournament");

    // ROSTER_ENTRY carries the ROSTER read's 16-byte entry.
    const std::vector<uint8_t> r = roster();
    // spec: tablet_begin/roster_anna
    // spec: tablet_repush/roster_anna
    const std::vector<uint8_t> anna = hex("01 01 00 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00");
    // spec: tablet_begin/roster_krystian
    // spec: tablet_repush/roster_krystian
    const std::vector<uint8_t> krystian = hex("01 01 01 44 33 22 11 00 40 ff 4b 52 59 53 54 49 41 4e 00");
    // spec: tablet_repush/roster_ola
    const std::vector<uint8_t> ola = hex("01 01 02 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00");
    const std::vector<uint8_t> *writes[] = {&anna, &krystian, &ola};
    for (uint8_t i = 0; i < 3; i++) {
        TournamentWriteFrame f;
        TEST_ASSERT_TRUE(parseTournamentWrite(writes[i]->data(), writes[i]->size(), f));
        TEST_ASSERT_TRUE(f.type == TournamentWrite::RosterEntry && f.index == i);
        TEST_ASSERT_EQUAL_MEMORY(&r[i * ROSTER_ENTRY_SIZE], f.entry, ROSTER_ENTRY_SIZE);
    }
    TEST_ASSERT_EQUAL_HEX32(TWO_ENTRY_CRC, rosterVersion(2, r.data()));

    // spec: tablet_begin/begin
    // spec: tablet_refusals/begin_on_menu
    WatchCommand cmd = parsed("01 11 01 01 e0 b1 7a 01 02 d4 f1 94 c6");
    TEST_ASSERT_TRUE(cmd.id == CommandId::BeginTournament && cmd.seq == 1);
    TEST_ASSERT_EQUAL_HEX32(TABLET_TID, cmd.tournamentId);
    TEST_ASSERT_TRUE(cmd.sport == static_cast<uint8_t>(SportId::Squash) && cmd.count == 2);
    TEST_ASSERT_EQUAL_HEX32(TWO_ENTRY_CRC, cmd.crc);

    // spec: tablet_begin/begin_ack
    expectChunks(chunksAt(0x61, tabletMatchStart(0x3, KRYSTIAN), 1, AckStatus::Applied, TWO_ENTRY_CRC),
                 "01 61 02 11 01 01 00 d4 f1 94 c6 03 00 00 00 0d 0c 0b 0a 44",
                 "01 61 12 33 22 11 00", "begin ack");

    TabletStatus st{};
    st.tournamentId = TABLET_TID;
    st.sport = static_cast<uint8_t>(SportId::Squash);
    st.rosterCount = 2;
    st.rosterCrc = TWO_ENTRY_CRC;
    st.outboxFrom = 1;
    // spec: tablet_begin/status_begun
    expectBuf("01 00 01 e0 b1 7a 01 02 d4 f1 94 c6 01 00 00 00 00 00 00 00", buildTournamentStatus(st), "begun");

    // spec: tablet_repush/repush
    cmd = parsed("01 11 02 01 e0 b1 7a 01 03 5b e7 0f 15");
    TEST_ASSERT_TRUE(cmd.count == 3 && cmd.crc == rosterVersion3());
    // spec: tablet_repush/repush_ack
    expectChunks(chunks(0x62, tabletMatchStart(0x7, KRYSTIAN), 2, AckStatus::Applied),
                 "01 62 02 11 01 02 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44",
                 "01 62 12 33 22 11 00", "repush ack");

    // Outbox.
    st.rosterCount = 3;
    st.rosterCrc = rosterVersion3();
    st.outboxTo = 2;
    // spec: tablet_outbox/status
    expectBuf("01 00 01 e0 b1 7a 01 03 5b e7 0f 15 01 00 02 00 00 00 00 00", buildTournamentStatus(st), "outbox");

    TournamentWriteFrame f;
    // spec: tablet_outbox/cursor_outbox
    const std::vector<uint8_t> toOutbox = hex("01 03 01 01 00");
    TEST_ASSERT_TRUE(parseTournamentWrite(toOutbox.data(), toOutbox.size(), f));
    TEST_ASSERT_TRUE(f.type == TournamentWrite::Cursor && f.which == TournamentRead::Outbox && f.seq == 1);

    const GameEntry tracked = {0x5EED0001, ANNA, KRYSTIAN, 11, 7, 0};
    const GameEntry offline = {0x5EED0002, KRYSTIAN, OLA, 11, 4, static_cast<uint8_t>(GameFlags::Offline)};
    uint8_t e[GAME_ENTRY_SIZE];
    buildGameEntry(tracked, e);
    // spec: tablet_outbox/read_1
    expectBuf("01 01 01 00 01 00 ed 5e 0d 0c 0b 0a 44 33 22 11 0b 07 00", buildOutboxRead(1, e), "outbox 1");
    buildGameEntry(offline, e);
    // spec: tablet_outbox/read_2
    expectBuf("01 01 02 00 02 00 ed 5e 44 33 22 11 88 77 66 55 0b 04 02", buildOutboxRead(2, e), "outbox 2");
    // spec: tablet_outbox/read_end
    expectBuf("01 02 03 00", buildOutboxEnd(3), "outbox end");

    // spec: tablet_outbox/ack
    cmd = parsed("01 13 03 01 e0 b1 7a 02 00");
    TEST_ASSERT_TRUE(cmd.id == CommandId::AckOutbox && cmd.tournamentId == TABLET_TID && cmd.upTo == 2);
    // spec: tablet_outbox/ack_ack
    expectChunks(chunks(0x63, tabletMatchStart(0x7, KRYSTIAN), 3, AckStatus::Applied),
                 "01 63 02 11 01 03 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44",
                 "01 63 12 33 22 11 00", "ack ack");

    // spec: tablet_outbox/cursor_status
    const std::vector<uint8_t> toStatus = hex("01 03 00 00 00");
    TEST_ASSERT_TRUE(parseTournamentWrite(toStatus.data(), toStatus.size(), f));
    TEST_ASSERT_TRUE(f.type == TournamentWrite::Cursor && f.which == TournamentRead::Status);
    st.outboxFrom = 3;
    // spec: tablet_outbox/status_acked
    expectBuf("01 00 01 e0 b1 7a 01 03 5b e7 0f 15 03 00 02 00 00 00 00 00", buildTournamentStatus(st), "acked");

    // Mirror.
    // spec: tablet_mirror/mirror_0
    const std::vector<uint8_t> m0 = hex("01 02 00 01 00 ed 5e 0d 0c 0b 0a 44 33 22 11 0b 07 01");
    // spec: tablet_mirror/mirror_1
    const std::vector<uint8_t> m1 = hex("01 02 01 02 00 ed 5e 44 33 22 11 88 77 66 55 0b 04 02");
    // spec: tablet_mirror/mirror_2
    const std::vector<uint8_t> m2 = hex("01 02 02 03 00 b1 7a 0d 0c 0b 0a 88 77 66 55 0b 09 00");
    const std::vector<uint8_t> *mirror[] = {&m0, &m1, &m2};
    uint8_t staged[3 * GAME_ENTRY_SIZE];
    for (uint8_t i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(parseTournamentWrite(mirror[i]->data(), mirror[i]->size(), f));
        TEST_ASSERT_TRUE(f.type == TournamentWrite::MirrorEntry && f.index == i);
        memcpy(staged + i * GAME_ENTRY_SIZE, f.entry, GAME_ENTRY_SIZE);
    }
    const GameEntry pending = parseGameEntry(staged);
    TEST_ASSERT_TRUE(pending.id == 0x5EED0001 && pending.uidA == ANNA && pending.uidB == KRYSTIAN);
    TEST_ASSERT_TRUE(pending.scoreA == 11 && pending.scoreB == 7
                     && pending.flags == static_cast<uint8_t>(GameFlags::Pending));
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(GameFlags::Offline), parseGameEntry(staged + 15).flags);
    TEST_ASSERT_EQUAL_HEX32(0x7AB10003, parseGameEntry(staged + 30).id);
    TEST_ASSERT_EQUAL_HEX32(0x8050D643, mirrorCrc(3, staged));
    TEST_ASSERT_EQUAL_HEX32(0xD202EF8D, mirrorCrc(0, nullptr));

    // spec: tablet_mirror/commit
    cmd = parsed("01 12 04 01 e0 b1 7a 03 43 d6 50 80");
    TEST_ASSERT_TRUE(cmd.id == CommandId::CommitMirror && cmd.tournamentId == TABLET_TID && cmd.count == 3);
    TEST_ASSERT_EQUAL_HEX32(0x8050D643, cmd.crc);
    // spec: tablet_mirror/commit_ack
    expectChunks(chunks(0x64, tabletMatchStart(0x7, KRYSTIAN), 4, AckStatus::Applied),
                 "01 64 02 11 01 04 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44",
                 "01 64 12 33 22 11 00", "commit ack");

    // RESULTS after the commit: the offline game, then the (+) game; the PENDING one is not listed.
    uint8_t listed[2 * RESULT_ENTRY_SIZE];
    buildResultEntry(KRYSTIAN, OLA, 11, 4, 1 | TURN_OFFLINE, listed);
    buildResultEntry(ANNA, OLA, 11, 9, 1, listed + RESULT_ENTRY_SIZE);
    // spec: tablet_mirror/results_2
    expectBuf("01 01 00 02 00 01 e0 b1 7a 0d 0c 0b 0a 88 77 66 55 0b 09 01", buildResultsRead(1, 2, TABLET_TID, 1, listed),
              "mirrored game");
    // spec: tablet_mirror/results_end
    expectBuf("01 02 00 02 00 01 e0 b1 7a 01 00", buildResultsRead(2, 2, TABLET_TID, 1, listed), "mirror rev");
    st.mirrorCrc = 0x8050D643;
    // spec: tablet_mirror/status_mirrored
    expectBuf("01 00 01 e0 b1 7a 01 03 5b e7 0f 15 03 00 02 00 43 d6 50 80", buildTournamentStatus(st), "mirrored");

    // spec: tablet_set_side/set_side
    cmd = parsed("01 14 05 01 88 77 66 55");
    TEST_ASSERT_TRUE(cmd.id == CommandId::SetSide && cmd.side == 1 && cmd.uid == OLA && cmd.hasValidSide());
    // spec: tablet_set_side/set_side_ack
    expectChunks(chunks(0x65, tabletMatchStart(0x7, OLA), 5, AckStatus::Applied),
                 "01 65 02 11 01 05 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 88",
                 "01 65 12 77 66 55 00", "set side ack");

    // spec: tablet_refusals/begin_other_tid
    cmd = parsed("01 11 06 02 e0 b1 7a 01 03 5b e7 0f 15");
    TEST_ASSERT_EQUAL_HEX32(0x7AB1E002, cmd.tournamentId);
    // spec: tablet_refusals/begin_other_tid_ack
    expectChunks(chunks(0x66, tabletMatchStart(0x7, OLA), 6, AckStatus::Invalid),
                 "01 66 02 11 01 06 02 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 88",
                 "01 66 12 77 66 55 00", "other tid ack");
    // spec: tablet_refusals/commit_bad_crc
    cmd = parsed("01 12 07 01 e0 b1 7a 00 00 00 00 00");
    TEST_ASSERT_TRUE(cmd.count == 0 && cmd.crc == 0 && cmd.crc != mirrorCrc(0, nullptr));
    // spec: tablet_refusals/commit_bad_crc_ack
    expectChunks(chunks(0x67, tabletMatchStart(0x7, OLA), 7, AckStatus::Invalid),
                 "01 67 02 11 01 07 02 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 88",
                 "01 67 12 77 66 55 00", "bad crc ack");
    // spec: tablet_refusals/remove
    cmd = parsed("01 10 08 01 e0 b1 7a 01 00 00 00");
    TEST_ASSERT_TRUE(cmd.id == CommandId::RemoveOfflineGame && cmd.tournamentId == TABLET_TID && cmd.rev == 1);
    // spec: tablet_refusals/remove_ack
    expectChunks(chunks(0x68, tabletMatchStart(0x7, OLA), 8, AckStatus::WrongScreen),
                 "01 68 02 11 01 08 01 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 88",
                 "01 68 12 77 66 55 00", "remove ack");
    // spec: tablet_refusals/back
    cmd = parsed("01 02 09");
    TEST_ASSERT_TRUE(cmd.id == CommandId::Back && cmd.seq == 9);
    // spec: tablet_refusals/back_ack
    expectChunks(chunks(0x69, tabletMatchStart(0x7, OLA), 9, AckStatus::WrongScreen),
                 "01 69 02 11 01 09 01 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 88",
                 "01 69 12 77 66 55 00", "back ack");

    WatchState menu;
    const SportId sports[] = {SportId::Padel, SportId::Squash, SportId::Volleyball, SportId::ShortVolleyball};
    menu.setMenu(SportId::Padel, sports, 4);
    const std::vector<Frame> mc = chunks(0x02, menu, 1, AckStatus::WrongScreen);
    TEST_ASSERT_EQUAL(1, mc.size());
    // spec: tablet_refusals/begin_on_menu_ack
    expectBuf("01 02 01 01 00 01 01 5b e7 0f 15 04 04 04 01 02 03", mc[0], "begin on MENU");
}

// Discards and lengths of the TOURNAMENT writes and the tablet commands.
void test_tablet_frame_edges() {
    TournamentWriteFrame f;
    const std::vector<uint8_t> anna = hex("01 01 00 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00");
    TEST_ASSERT_FALSE_MESSAGE(parseTournamentWrite(anna.data(), anna.size() - 1, f), "short roster entry");
    std::vector<uint8_t> v = anna;
    v[0] = 0x00;
    TEST_ASSERT_FALSE_MESSAGE(parseTournamentWrite(v.data(), v.size(), f), "foreign PROTO");
    v = anna;
    v[2] = TABLET_ROSTER_MAX;
    TEST_ASSERT_FALSE_MESSAGE(parseTournamentWrite(v.data(), v.size(), f), "roster index past the stage");
    v[2] = TABLET_ROSTER_MAX - 1;
    TEST_ASSERT_TRUE(parseTournamentWrite(v.data(), v.size(), f) && f.index == TABLET_ROSTER_MAX - 1);
    v.push_back(0xEE);
    TEST_ASSERT_TRUE_MESSAGE(parseTournamentWrite(v.data(), v.size(), f), "trailing bytes ignored");

    std::vector<uint8_t> game = hex("01 02 7f 01 00 ed 5e 0d 0c 0b 0a 44 33 22 11 0b 07 01");
    TEST_ASSERT_TRUE(parseTournamentWrite(game.data(), game.size(), f) && f.index == 127);
    TEST_ASSERT_FALSE_MESSAGE(parseTournamentWrite(game.data(), game.size() - 1, f), "short game");
    game[2] = 128;
    TEST_ASSERT_FALSE_MESSAGE(parseTournamentWrite(game.data(), game.size(), f), "game index past the stage");

    const std::vector<uint8_t> end = hex("01 03 02 00 00");
    TEST_ASSERT_FALSE_MESSAGE(parseTournamentWrite(end.data(), end.size(), f), "cursor OUTBOX_END");
    const std::vector<uint8_t> cursor = hex("01 03 01 34 12");
    TEST_ASSERT_FALSE_MESSAGE(parseTournamentWrite(cursor.data(), 4, f), "short cursor");
    TEST_ASSERT_TRUE(parseTournamentWrite(cursor.data(), cursor.size(), f) && f.seq == 0x1234);
    const std::vector<uint8_t> unknown = hex("01 04 00 00 00");
    TEST_ASSERT_FALSE_MESSAGE(parseTournamentWrite(unknown.data(), unknown.size(), f), "unknown type");
    TEST_ASSERT_FALSE_MESSAGE(parseTournamentWrite(unknown.data(), 1, f), "no type");

    TEST_ASSERT_EQUAL_UINT8(13, commandSize(static_cast<uint8_t>(CommandId::BeginTournament)));
    TEST_ASSERT_EQUAL_UINT8(12, commandSize(static_cast<uint8_t>(CommandId::CommitMirror)));
    TEST_ASSERT_EQUAL_UINT8(9, commandSize(static_cast<uint8_t>(CommandId::AckOutbox)));
    TEST_ASSERT_EQUAL_UINT8(8, commandSize(static_cast<uint8_t>(CommandId::SetSide)));
    WatchCommand cmd{};
    const std::vector<uint8_t> setSide = hex("01 14 05 01 88 77 66 55");
    TEST_ASSERT_TRUE(parseCommand(setSide.data(), setSide.size() - 1, cmd) == CommandParse::Malformed && cmd.seq == 5);
    const std::vector<uint8_t> ack = hex("01 13 03 01 e0 b1 7a 34 12");
    TEST_ASSERT_TRUE(parseCommand(ack.data(), ack.size(), cmd) == CommandParse::Ok && cmd.upTo == 0x1234);
    TEST_ASSERT_EQUAL_UINT8(20, buildTournamentStatus(TabletStatus{}).len);
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
    RUN_TEST(test_sides_swapped_state);
    RUN_TEST(test_last_point_state);
    RUN_TEST(test_state_change_detection);
    RUN_TEST(test_menu_clamped_to_buffer);
    RUN_TEST(test_chunk_boundaries);
    RUN_TEST(test_command_sizes);
    RUN_TEST(test_command_unknown_and_dropped);
    RUN_TEST(test_command_arguments);
    RUN_TEST(test_focus);
    RUN_TEST(test_service_uuid_bytes);
    RUN_TEST(test_heart_frames);
    RUN_TEST(test_results_frames);
    RUN_TEST(test_offline_games);
    RUN_TEST(test_tablet_frames);
    RUN_TEST(test_tablet_frame_edges);
    return UNITY_END();
}
