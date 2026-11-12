#ifndef GARMIN_PROTOCOL_H
#define GARMIN_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * Garmin App Remote wire format (docs/garmin-protocol.md, v1). Pure byte work: no Arduino,
 * no NimBLE, so the host suites test exactly what the board ships.
 */

namespace Garmin {
    // The board's protocol version. Every frame starts with it.
    constexpr uint8_t PROTO = 0x01;

    constexpr uint16_t COMPANY_ID = 0xFFFF;

    constexpr uint8_t MAX_FRAME = 20;
    constexpr uint8_t MAX_ADV = 31;
    constexpr uint8_t CHUNK_HEADER = 3;
    constexpr uint8_t CHUNK_BODY = 17;
    constexpr uint8_t MAX_CHUNKS = 15;
    constexpr uint8_t MAX_STATE_BODY = 255;
    constexpr uint8_t STATE_HEADER = 8;

    constexpr uint8_t KEY_SIZE = 16;
    constexpr uint8_t NONCE_SIZE = 8;
    constexpr uint8_t MAC_SIZE = 8;
    constexpr uint8_t SLOT_COUNT = 8;
    constexpr uint8_t MAX_CONNECTIONS = 4;
    constexpr uint8_t ROSTER_ENTRY_SIZE = 16;
    constexpr uint8_t ROSTER_NAME_SIZE = 9;
    constexpr uint8_t VERSION_TEXT_MAX = 19;

    constexpr uint16_t PAIRING_CODE_COUNT = 10000;
    constexpr uint32_t PAIRING_WINDOW_MS = 120000;
    constexpr uint32_t UNAUTH_TIMEOUT_MS = 10000;
    constexpr uint32_t FAILED_PROOF_DISCONNECT_MS = 2000;

    // HEART notify (spec 18): a fixed wire size, room for every other connection.
    constexpr uint8_t HEART_LIST = 3;
    static_assert(MAX_CONNECTIONS - 1 <= HEART_LIST, "a HEART notify must fit every other connection");
    constexpr uint32_t HEART_EXPIRY_MS = 15000;
    constexpr uint32_t HEART_MIN_INTERVAL_MS = 1000;

    // RESULTS (spec 19): header PROTO, index u16, count u16, tournamentId u32; then one entry.
    constexpr uint8_t RESULTS_HEADER = 9;
    constexpr uint8_t RESULT_ENTRY_SIZE = 11;
    constexpr uint16_t RESULTS_MAX = 128;
    static_assert(RESULTS_HEADER + RESULT_ENTRY_SIZE <= MAX_FRAME, "a RESULTS read must fit one frame");
    // Past the end: the header + rev u16. 11 B also tells a client the board takes offline games.
    constexpr uint8_t RESULTS_END_FRAME = RESULTS_HEADER + 2;
    // Entry `turn` bit 7: a client-added game; bits 0-6 the turn.
    constexpr uint8_t TURN_OFFLINE = 0x80;
    constexpr uint8_t TURN_MASK = 0x7F;

    // TOURNAMENT (spec 20).
    constexpr uint8_t TABLET_ROSTER_MAX = 16;
    // id u32, uidA, uidB, scoreA, scoreB, flags.
    constexpr uint8_t GAME_ENTRY_SIZE = 15;
    constexpr uint8_t TOURNAMENT_STATUS_FRAME = 20;
    constexpr uint8_t OUTBOX_FRAME = 4 + GAME_ENTRY_SIZE;
    constexpr uint8_t OUTBOX_END_FRAME = 4;
    static_assert(OUTBOX_FRAME <= MAX_FRAME, "an OUTBOX read must fit one frame");

    constexpr uint8_t ADV_FLAG_PAIRING = 0x01;

    constexpr const char *SERVICE_UUID = "0d780001-2f44-4f31-93b7-b78dc9e29735";
    constexpr const char *VERSION_UUID = "0d780002-2f44-4f31-93b7-b78dc9e29735";
    constexpr const char *COMMAND_UUID = "0d780003-2f44-4f31-93b7-b78dc9e29735";
    constexpr const char *STATE_UUID = "0d780004-2f44-4f31-93b7-b78dc9e29735";
    constexpr const char *ROSTER_UUID = "0d780005-2f44-4f31-93b7-b78dc9e29735";
    constexpr const char *AUTH_UUID = "0d780006-2f44-4f31-93b7-b78dc9e29735";
    constexpr const char *PAIRING_UUID = "0d780007-2f44-4f31-93b7-b78dc9e29735";
    constexpr const char *HEART_UUID = "0d780008-2f44-4f31-93b7-b78dc9e29735";
    constexpr const char *RESULTS_UUID = "0d780009-2f44-4f31-93b7-b78dc9e29735";
    constexpr const char *TOURNAMENT_UUID = "0d78000a-2f44-4f31-93b7-b78dc9e29735";

    enum class AckStatus : uint8_t {
        Applied = 0,
        WrongScreen = 1,
        Invalid = 2,
        UnknownCmd = 3,
        Busy = 4,
        Malformed = 5,
    };

    enum class ScreenId : uint8_t {
        Booting = 0x00,
        Menu = 0x01,
        Config = 0x02,
        Profile = 0x03,
        TournamentWait = 0x04,
        ChoosePlayers = 0x10,
        MatchStart = 0x11,
        Intro = 0x12,
        Playing = 0x13,
        Celebration = 0x14,
        GameOver = 0x15,
    };

    enum class SportId : uint8_t {
        None = 0x00,
        Squash = 0x01,
        Volleyball = 0x02,
        ShortVolleyball = 0x03,
        Padel = 0x04,
    };

    // Which roster a screen's uids belong to (spec 12). Never on the wire: the watch derives it from the sport.
    enum class RosterKind : uint8_t { Players = 0, Teams = 1 };

    // 0x0C (END_MATCH) was dropped in development and stays unused, so it parses as UnknownCmd.
    enum class CommandId : uint8_t {
        SelectSport = 0x01,
        Back = 0x02,
        TogglePlayer = 0x03,
        StartTournament = 0x04,
        SetPair = 0x05,
        SwapSides = 0x06,
        StartMatch = 0x07,
        Score = 0x08,
        Undo = 0x09,
        Skip = 0x0A,
        NextGame = 0x0B,
        Sync = 0x0D,
        Focus = 0x0E,
        AddOfflineGame = 0x0F,
        RemoveOfflineGame = 0x10,
        BeginTournament = 0x11,
        CommitMirror = 0x12,
        AckOutbox = 0x13,
        SetSide = 0x14,
    };

    enum class AuthType : uint8_t { Challenge = 0x00, Proof = 0x01, Result = 0x02 };

    enum class AuthStatus : uint8_t { Ok = 0, BadProof = 1, UnknownSlot = 2 };

    enum class PairingType : uint8_t { None = 0x00, Request = 0x01, Result = 0x02 };

    enum class PairingStatus : uint8_t { Ok = 0, WindowClosed = 1, BadCode = 2, FullRetry = 3 };

    enum class TournamentWrite : uint8_t { RosterEntry = 0x01, MirrorEntry = 0x02, Cursor = 0x03 };

    // Also the CURSOR write's `which` (OutboxEnd there is discarded).
    enum class TournamentRead : uint8_t { Status = 0x00, Outbox = 0x01, OutboxEnd = 0x02 };

    enum class GameFlags : uint8_t { Pending = 0x01, Offline = 0x02 };

    template<uint8_t N>
    struct ByteBuf {
        uint8_t data[N];
        uint8_t len;
    };

    typedef ByteBuf<MAX_FRAME> Frame;
    typedef ByteBuf<MAX_ADV> AdvData;

    inline void putU16(uint8_t *out, const uint16_t v) {
        out[0] = static_cast<uint8_t>(v);
        out[1] = static_cast<uint8_t>(v >> 8);
    }

    inline void putU32(uint8_t *out, const uint32_t v) {
        out[0] = static_cast<uint8_t>(v);
        out[1] = static_cast<uint8_t>(v >> 8);
        out[2] = static_cast<uint8_t>(v >> 16);
        out[3] = static_cast<uint8_t>(v >> 24);
    }

    inline uint16_t getU16(const uint8_t *in) {
        return static_cast<uint16_t>(in[0] | (in[1] << 8));
    }

    inline uint32_t getU32(const uint8_t *in) {
        return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8)
               | (static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
    }

    // The 128-bit service UUID in BLE (little-endian) byte order, for SCAN_RSP.
    inline void serviceUuidLe(uint8_t out[16]) {
        constexpr uint8_t BE[16] = {
            0x0d, 0x78, 0x00, 0x01, 0x2f, 0x44, 0x4f, 0x31,
            0x93, 0xb7, 0xb7, 0x8d, 0xc9, 0xe2, 0x97, 0x35,
        };
        for (uint8_t i = 0; i < 16; i++) out[i] = BE[15 - i];
    }

    // Uniform enough for a code that only makes the user pick the board in front of them.
    inline uint16_t pairingCodeFrom(const uint32_t random) {
        return static_cast<uint16_t>(random % PAIRING_CODE_COUNT);
    }

    // --- Advertising (4.2, 4.3) ---

    inline AdvData buildAdvertising(const uint32_t boardId, const bool pairing, const uint16_t code) {
        AdvData a{};
        uint8_t *p = a.data;
        p[0] = 0x0F;
        p[1] = 0xFF;
        putU16(p + 2, COMPANY_ID);
        p[4] = 0x53; // SCBD
        p[5] = 0x43;
        p[6] = 0x42;
        p[7] = 0x44;
        p[8] = PROTO;
        putU32(p + 9, boardId);
        p[13] = pairing ? ADV_FLAG_PAIRING : 0;
        putU16(p + 14, pairing ? code : 0);
        p[16] = 0x02;
        p[17] = 0x01;
        p[18] = 0x06;
        a.len = 19;
        return a;
    }

    inline AdvData buildScanResponse(const uint32_t boardId) {
        AdvData a{};
        uint8_t *p = a.data;
        p[0] = 0x11;
        p[1] = 0x07;
        serviceUuidLe(p + 2);
        p[18] = 0x0B;
        p[19] = 0x09;
        memcpy(p + 20, "Score-", 6);
        const char *hex = "0123456789ABCDEF";
        for (uint8_t i = 0; i < 4; i++) {
            p[26 + i] = static_cast<uint8_t>(hex[(boardId >> (12 - 4 * i)) & 0x0F]);
        }
        a.len = 30;
        return a;
    }

    // --- Small frames ---

    // Unauthenticated reads of STATE and ROSTER.
    inline Frame buildProtoOnly() {
        Frame f{};
        f.data[0] = PROTO;
        f.len = 1;
        return f;
    }

    // Firmware text is cut at 19 characters (or its first NUL) to keep the 20 B bound.
    inline Frame buildVersion(const char *firmware) {
        Frame f{};
        f.data[0] = PROTO;
        uint8_t n = 0;
        while (firmware && firmware[n] != '\0' && n < VERSION_TEXT_MAX) {
            f.data[1 + n] = static_cast<uint8_t>(firmware[n]);
            n++;
        }
        f.len = static_cast<uint8_t>(1 + n);
        return f;
    }

    // --- AUTH (8.1) ---

    struct AuthProof {
        uint8_t slotId;
        uint8_t nw[NONCE_SIZE];
        uint8_t mac[MAC_SIZE];
    };

    inline Frame buildAuthChallenge(const uint8_t nb[NONCE_SIZE]) {
        Frame f{};
        f.data[0] = PROTO;
        f.data[1] = static_cast<uint8_t>(AuthType::Challenge);
        memcpy(f.data + 2, nb, NONCE_SIZE);
        f.len = 10;
        return f;
    }

    // False = not a proof (foreign PROTO, wrong type, too short): ignore the write.
    inline bool parseAuthProof(const uint8_t *in, const size_t len, AuthProof &out) {
        if (len < 19 || in[0] != PROTO || in[1] != static_cast<uint8_t>(AuthType::Proof)) return false;
        out.slotId = in[2];
        memcpy(out.nw, in + 3, NONCE_SIZE);
        memcpy(out.mac, in + 11, MAC_SIZE);
        return true;
    }

    inline Frame buildAuthResultOk(const uint8_t boardMac[MAC_SIZE]) {
        Frame f{};
        f.data[0] = PROTO;
        f.data[1] = static_cast<uint8_t>(AuthType::Result);
        f.data[2] = static_cast<uint8_t>(AuthStatus::Ok);
        memcpy(f.data + 3, boardMac, MAC_SIZE);
        f.len = 11;
        return f;
    }

    inline Frame buildAuthResultFail(const AuthStatus status) {
        Frame f{};
        f.data[0] = PROTO;
        f.data[1] = static_cast<uint8_t>(AuthType::Result);
        f.data[2] = static_cast<uint8_t>(status);
        f.len = 3;
        return f;
    }

    // --- PAIRING (9.1) ---

    inline bool parsePairingRequest(const uint8_t *in, const size_t len, uint16_t &code) {
        if (len < 4 || in[0] != PROTO || in[1] != static_cast<uint8_t>(PairingType::Request)) return false;
        code = getU16(in + 2);
        return true;
    }

    inline Frame buildPairingNone() {
        Frame f{};
        f.data[0] = PROTO;
        f.data[1] = static_cast<uint8_t>(PairingType::None);
        f.len = 2;
        return f;
    }

    inline Frame buildPairingOk(const uint8_t slotId, const uint8_t key[KEY_SIZE]) {
        Frame f{};
        f.data[0] = PROTO;
        f.data[1] = static_cast<uint8_t>(PairingType::Result);
        f.data[2] = static_cast<uint8_t>(PairingStatus::Ok);
        f.data[3] = slotId;
        memcpy(f.data + 4, key, KEY_SIZE);
        f.len = 20;
        return f;
    }

    inline Frame buildPairingFail(const PairingStatus status) {
        Frame f{};
        f.data[0] = PROTO;
        f.data[1] = static_cast<uint8_t>(PairingType::Result);
        f.data[2] = static_cast<uint8_t>(status);
        f.len = 3;
        return f;
    }

    // --- Roster (12) ---

    // zlib crc32 (ISO-HDLC). Chain calls by passing the previous result as `crc`.
    inline uint32_t crc32(const uint8_t *data, const size_t len, const uint32_t crc = 0) {
        uint32_t c = ~crc;
        for (size_t i = 0; i < len; i++) {
            c ^= data[i];
            for (uint8_t b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        return ~c;
    }

    // uid, r, g, b, then up to 9 name characters NUL-padded (a 9-character name has no NUL).
    inline void buildRosterEntry(const uint32_t uid, const uint8_t r, const uint8_t g, const uint8_t b,
                                 const char *name, uint8_t out[ROSTER_ENTRY_SIZE]) {
        putU32(out, uid);
        out[4] = r;
        out[5] = g;
        out[6] = b;
        memset(out + 7, 0, ROSTER_NAME_SIZE);
        for (uint8_t i = 0; name && i < ROSTER_NAME_SIZE && name[i] != '\0'; i++) {
            out[7 + i] = static_cast<uint8_t>(name[i]);
        }
    }

    // `entries` = count * 16 wire bytes in index order.
    inline uint32_t rosterVersion(const uint8_t count, const uint8_t *entries) {
        const uint32_t c = crc32(&count, 1);
        return crc32(entries, static_cast<size_t>(count) * ROSTER_ENTRY_SIZE, c);
    }

    // ROSTER write: sets the per-connection cursor.
    inline bool parseRosterCursor(const uint8_t *in, const size_t len, uint8_t &index) {
        if (len < 2 || in[0] != PROTO) return false;
        index = in[1];
        return true;
    }

    // 19 B for an entry, 3 B past the end. The caller advances the cursor after a read.
    inline Frame buildRosterRead(const uint8_t index, const uint8_t count, const uint8_t *entries) {
        Frame f{};
        f.data[0] = PROTO;
        f.data[1] = index;
        f.data[2] = count;
        if (index >= count) {
            f.len = 3;
            return f;
        }
        memcpy(f.data + 3, entries + static_cast<size_t>(index) * ROSTER_ENTRY_SIZE, ROSTER_ENTRY_SIZE);
        f.len = 19;
        return f;
    }

    // --- HEART (18) ---

    // False = not a heart write (foreign PROTO, too short): discard. bpm 0 clears the sender's value.
    inline bool parseHeartWrite(const uint8_t *in, const size_t len, uint8_t &bpm) {
        if (len < 2 || in[0] != PROTO) return false;
        bpm = in[1];
        return true;
    }

    inline Frame buildHeart(const uint8_t bpm[HEART_LIST]) {
        Frame f{};
        f.data[0] = PROTO;
        memcpy(f.data + 1, bpm, HEART_LIST);
        f.len = static_cast<uint8_t>(1 + HEART_LIST);
        return f;
    }

    // --- RESULTS (19) ---

    // A/B = the match's players in creation order, not court sides; turn = this pair's k-th game (1-based).
    inline void buildResultEntry(const uint32_t uidA, const uint32_t uidB, const uint8_t scoreA,
                                 const uint8_t scoreB, const uint8_t turn, uint8_t out[RESULT_ENTRY_SIZE]) {
        putU32(out, uidA);
        putU32(out + 4, uidB);
        out[8] = scoreA;
        out[9] = scoreB;
        out[10] = turn;
    }

    // RESULTS write: [PROTO, index u16]; shorter or a foreign PROTO is discarded.
    inline bool parseResultsCursor(const uint8_t *in, const size_t len, uint16_t &index) {
        if (len < 3 || in[0] != PROTO) return false;
        index = getU16(in + 1);
        return true;
    }

    // 20 B for an entry, the header + rev past the end. The caller advances the cursor after a read.
    inline Frame buildResultsRead(const uint16_t index, const uint16_t count, const uint32_t tournamentId,
                                  const uint16_t rev, const uint8_t *entries) {
        Frame f{};
        f.data[0] = PROTO;
        putU16(f.data + 1, index);
        putU16(f.data + 3, count);
        putU32(f.data + 5, tournamentId);
        if (index >= count) {
            putU16(f.data + RESULTS_HEADER, rev);
            f.len = RESULTS_END_FRAME;
            return f;
        }
        memcpy(f.data + RESULTS_HEADER, entries + static_cast<size_t>(index) * RESULT_ENTRY_SIZE,
               RESULT_ENTRY_SIZE);
        f.len = RESULTS_HEADER + RESULT_ENTRY_SIZE;
        return f;
    }

    // --- TOURNAMENT (20) ---

    struct GameEntry {
        uint32_t id;
        uint32_t uidA;
        uint32_t uidB;
        uint8_t scoreA;
        uint8_t scoreB;
        uint8_t flags;
    };

    inline void buildGameEntry(const GameEntry &g, uint8_t out[GAME_ENTRY_SIZE]) {
        putU32(out, g.id);
        putU32(out + 4, g.uidA);
        putU32(out + 8, g.uidB);
        out[12] = g.scoreA;
        out[13] = g.scoreB;
        out[14] = g.flags;
    }

    inline GameEntry parseGameEntry(const uint8_t in[GAME_ENTRY_SIZE]) {
        GameEntry g;
        g.id = getU32(in);
        g.uidA = getU32(in + 4);
        g.uidB = getU32(in + 8);
        g.scoreA = in[12];
        g.scoreB = in[13];
        g.flags = in[14];
        return g;
    }

    // `entries` = count * 15 wire bytes in index order; the same formula as rosterVersion.
    inline uint32_t mirrorCrc(const uint8_t count, const uint8_t *entries) {
        const uint32_t c = crc32(&count, 1);
        return crc32(entries, static_cast<size_t>(count) * GAME_ENTRY_SIZE, c);
    }

    struct TournamentWriteFrame {
        TournamentWrite type;
        uint8_t index;          // ROSTER_ENTRY, MIRROR_ENTRY
        const uint8_t *entry;   // into the input: 16 B roster entry or 15 B game
        TournamentRead which;   // CURSOR: Status or Outbox
        uint16_t seq;           // CURSOR
    };

    // False = discard: foreign PROTO, unknown type, too short, index past its stage, or a
    // cursor `which` other than STATUS / OUTBOX.
    inline bool parseTournamentWrite(const uint8_t *in, const size_t len, TournamentWriteFrame &out) {
        if (len < 2 || in[0] != PROTO) return false;
        out = TournamentWriteFrame{};
        out.type = static_cast<TournamentWrite>(in[1]);
        switch (out.type) {
            case TournamentWrite::RosterEntry:
                if (len < 3 + ROSTER_ENTRY_SIZE || in[2] >= TABLET_ROSTER_MAX) return false;
                out.index = in[2];
                out.entry = in + 3;
                return true;
            case TournamentWrite::MirrorEntry:
                if (len < 3 + GAME_ENTRY_SIZE || in[2] >= RESULTS_MAX) return false;
                out.index = in[2];
                out.entry = in + 3;
                return true;
            case TournamentWrite::Cursor:
                if (len < 5 || in[2] > static_cast<uint8_t>(TournamentRead::Outbox)) return false;
                out.which = static_cast<TournamentRead>(in[2]);
                out.seq = getU16(in + 3);
                return true;
        }
        return false;
    }

    struct TabletStatus {
        uint32_t tournamentId;  // 0 = none; then every field is 0
        uint8_t sport;
        uint8_t rosterCount;
        uint32_t rosterCrc;
        uint16_t outboxFrom;
        uint16_t outboxTo;
        uint32_t mirrorCrc;
    };

    inline Frame buildTournamentStatus(const TabletStatus &s) {
        Frame f{};
        f.data[0] = PROTO;
        f.data[1] = static_cast<uint8_t>(TournamentRead::Status);
        putU32(f.data + 2, s.tournamentId);
        f.data[6] = s.sport;
        f.data[7] = s.rosterCount;
        putU32(f.data + 8, s.rosterCrc);
        putU16(f.data + 12, s.outboxFrom);
        putU16(f.data + 14, s.outboxTo);
        putU32(f.data + 16, s.mirrorCrc);
        f.len = TOURNAMENT_STATUS_FRAME;
        return f;
    }

    inline Frame buildOutboxRead(const uint16_t seq, const uint8_t entry[GAME_ENTRY_SIZE]) {
        Frame f{};
        f.data[0] = PROTO;
        f.data[1] = static_cast<uint8_t>(TournamentRead::Outbox);
        putU16(f.data + 2, seq);
        memcpy(f.data + 4, entry, GAME_ENTRY_SIZE);
        f.len = OUTBOX_FRAME;
        return f;
    }

    inline Frame buildOutboxEnd(const uint16_t cursor) {
        Frame f{};
        f.data[0] = PROTO;
        f.data[1] = static_cast<uint8_t>(TournamentRead::OutboxEnd);
        putU16(f.data + 2, cursor);
        f.len = OUTBOX_END_FRAME;
        return f;
    }

    // --- State chunks (11.1) ---

    // 0 for an empty or oversized body: nothing to send.
    inline uint8_t chunkCount(const size_t bodyLen) {
        if (bodyLen == 0 || bodyLen > MAX_STATE_BODY) return 0;
        return static_cast<uint8_t>((bodyLen + CHUNK_BODY - 1) / CHUNK_BODY);
    }

    // len 0 when `index` is out of range.
    inline Frame buildStateChunk(const uint8_t stateSeq, const uint8_t *body, const size_t bodyLen,
                                 const uint8_t index) {
        Frame f{};
        const uint8_t count = chunkCount(bodyLen);
        if (index >= count) return f;
        const size_t from = static_cast<size_t>(index) * CHUNK_BODY;
        const size_t n = bodyLen - from < CHUNK_BODY ? bodyLen - from : CHUNK_BODY;
        f.data[0] = PROTO;
        f.data[1] = stateSeq;
        f.data[2] = static_cast<uint8_t>(index << 4 | count);
        memcpy(f.data + CHUNK_HEADER, body + from, n);
        f.len = static_cast<uint8_t>(CHUNK_HEADER + n);
        return f;
    }
}

#endif //GARMIN_PROTOCOL_H
