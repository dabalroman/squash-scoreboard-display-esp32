// GarminLink, the board's per-connection protocol state (docs/garmin-protocol.md 6-12):
// auth gate, single-use proof, pairing window, dedupe and acks, pushes and timeouts, against
// the spec's vectors. Plus the DeviceModeState <-> sport table. Board-agnostic.
#include <unity.h>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "Garmin/GarminLink.h"
#include "Garmin/WatchSport.h"
#include "../common/check.h"
#include "../common/host_globals.h"

void setUp() {}
void tearDown() {}

namespace {
    struct NoGuard {
        NoGuard() {}
    };

    typedef GarminLinkT<NoGuard> Link;

    std::deque<std::vector<uint8_t>> g_fills;
    uint32_t g_word = 0;

    void fakeFill(uint8_t *out, const size_t len) {
        std::vector<uint8_t> next;
        if (!g_fills.empty()) {
            next = g_fills.front();
            g_fills.pop_front();
        }
        for (size_t i = 0; i < len; i++) out[i] = i < next.size() ? next[i] : 0x55;
    }

    uint32_t fakeWord() {
        return g_word;
    }

    std::vector<uint8_t> hex(const char *text) {
        std::vector<uint8_t> out;
        int nibble = -1;
        for (const char *p = text; *p; p++) {
            int v;
            if (*p >= '0' && *p <= '9') v = *p - '0';
            else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
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

    std::vector<uint8_t> seq(const uint8_t from, const uint8_t count) {
        std::vector<uint8_t> out;
        for (uint8_t i = 0; i < count; i++) out.push_back(static_cast<uint8_t>(from + i));
        return out;
    }

    std::string dump(const uint8_t *data, const size_t len) {
        std::string s;
        for (size_t i = 0; i < len; i++) s += strf("%02x", data[i]);
        return s;
    }

    void expectFrame(const char *expectedHex, const Garmin::Frame &frame, const char *what) {
        const std::vector<uint8_t> expected = hex(expectedHex);
        CHECK(expected.size() == frame.len && memcmp(expected.data(), frame.data, frame.len) == 0,
              "%s: got %s, expected %s", what, dump(frame.data, frame.len).c_str(),
              dump(expected.data(), expected.size()).c_str());
    }

    void put(Link &link, void (Link::*fn)(uint16_t, const uint8_t *, size_t), const uint16_t handle,
               const char *frameHex) {
        const std::vector<uint8_t> f = hex(frameHex);
        (link.*fn)(handle, f.data(), f.size());
    }

    void putAt(Link &link, void (Link::*fn)(uint16_t, const uint8_t *, size_t, uint32_t), const uint16_t handle,
                 const char *frameHex, const uint32_t now) {
        const std::vector<uint8_t> f = hex(frameHex);
        (link.*fn)(handle, f.data(), f.size(), now);
    }

    // Spec 16: key 00..0f in slot 2 (slots 0 and 1 hold other keys).
    void loadSpecKeys(Link &link) {
        GarminData data;
        GarminPairingStore::fresh(data);
        data.enabled = 1;
        data.boardId = 0x12345678;
        for (uint8_t s = 0; s < 3; s++) {
            data.slots[s].used = 1;
            data.slots[s].lastUsed = s + 1;
            const std::vector<uint8_t> key = s == 2 ? seq(0x00, 16) : seq(static_cast<uint8_t>(0x80 + 16 * s), 16);
            memcpy(data.slots[s].key, key.data(), 16);
        }
        TEST_ASSERT_TRUE(link.loadData(reinterpret_cast<const uint8_t *>(&data), sizeof(data)));
    }

    const char *SPEC_PROOF = "00 01 02 b0 b1 b2 b3 b4 b5 b6 b7 4e 47 0a 94 b9 24 ac ae";

    void connectWithSpecNonce(Link &link, const uint16_t handle, const uint32_t now) {
        g_fills.push_back(seq(0xa0, 8));
        TEST_ASSERT_TRUE(link.connect(handle, now));
    }

    void authenticate(Link &link, const uint16_t handle, const uint32_t now) {
        connectWithSpecNonce(link, handle, now);
        putAt(link, &Link::writeAuth, handle, SPEC_PROOF, now);
        CHECK(link.readAuth(handle).len == 11, "authentication of %u failed", handle);
    }

    // Spec 16.4's roster.
    void loadSpecRoster(Link &link) {
        uint8_t entries[3 * Garmin::ROSTER_ENTRY_SIZE];
        Garmin::buildRosterEntry(0x0A0B0C0D, 0xff, 0x00, 0x00, "ANNA", entries);
        Garmin::buildRosterEntry(0x11223344, 0x00, 0x40, 0xff, "KRYSTIAN", entries + 16);
        Garmin::buildRosterEntry(0x55667788, 0x00, 0xc0, 0x30, "OLA", entries + 32);
        link.setRoster(3, entries);
    }

    WatchState menuState() {
        const Garmin::SportId sports[] = {Garmin::SportId::Padel, Garmin::SportId::Squash,
                                          Garmin::SportId::Volleyball, Garmin::SportId::ShortVolleyball};
        WatchState s;
        s.setMenu(Garmin::SportId::Padel, sports, 4);
        return s;
    }

    Garmin::Frame chunk(const Link::Push &p, const uint8_t index) {
        return Garmin::buildStateChunk(p.stateSeq, p.body, p.len, index);
    }
}

void test_auth_spec_vector() {
    Link link(fakeFill, fakeWord);
    loadSpecKeys(link);
    GarminData out;
    link.takeDirtyData(out);

    connectWithSpecNonce(link, 1, 0);
    expectFrame("00 00 a0 a1 a2 a3 a4 a5 a6 a7", link.readAuth(1), "challenge");
    putAt(link, &Link::writeAuth, 1, SPEC_PROOF, 0);
    expectFrame("00 02 00 18 57 e1 b4 a9 6e bf 5a", link.readAuth(1), "result");
    TEST_ASSERT_EQUAL(1, link.authedCount());

    // The LRU bump is persisted from loop(), never from the host task.
    TEST_ASSERT_TRUE(link.takeDirtyData(out));
    TEST_ASSERT_TRUE(out.slots[2].lastUsed > out.slots[0].lastUsed);
    TEST_ASSERT_TRUE(out.slots[2].lastUsed > out.slots[1].lastUsed);
    TEST_ASSERT_FALSE(link.takeDirtyData(out));
}

void test_unauthenticated_is_gated() {
    Link link(fakeFill, fakeWord);
    loadSpecKeys(link);
    loadSpecRoster(link);
    connectWithSpecNonce(link, 1, 0);

    expectFrame("00", link.readState(1), "state");
    put(link, &Link::writeRoster, 1, "00 01");
    expectFrame("00", link.readRoster(1), "roster");
    put(link, &Link::writeCommand, 1, "00 08 07 00");
    Link::Queued q;
    TEST_ASSERT_FALSE(link.takeCommand(q));

    link.subscribe(1, Link::Sub::State, true);
    Link::Push pushes[Garmin::MAX_CONNECTIONS];
    const WatchState s = menuState();
    TEST_ASSERT_EQUAL(0, link.preparePushes(&s, 0, pushes));

    // Unknown connection: nothing but the 1-byte frame.
    expectFrame("00", link.readAuth(9), "unknown connection");
}

void test_proof_is_single_use() {
    Link link(fakeFill, fakeWord);
    loadSpecKeys(link);

    connectWithSpecNonce(link, 1, 1000);
    putAt(link, &Link::writeAuth, 1, "00 01 02 b0 b1 b2 b3 b4 b5 b6 b7 4e 47 0a 94 b9 24 ac af", 1000);
    expectFrame("00 02 01", link.readAuth(1), "bad proof");
    // The right proof after a wrong one is ignored.
    putAt(link, &Link::writeAuth, 1, SPEC_PROOF, 1100);
    expectFrame("00 02 01", link.readAuth(1), "second proof");
    TEST_ASSERT_EQUAL(0, link.authedCount());

    uint16_t due[Garmin::MAX_CONNECTIONS];
    TEST_ASSERT_EQUAL(0, link.dueDisconnects(2999, due));
    TEST_ASSERT_EQUAL(1, link.dueDisconnects(3000, due));
    TEST_ASSERT_EQUAL(1, due[0]);
    // Not re-issued every tick.
    TEST_ASSERT_EQUAL(0, link.dueDisconnects(3050, due));
    TEST_ASSERT_EQUAL(1, link.dueDisconnects(4000, due));

    // A slot with no key, and a foreign PROTO (ignored, no proof used).
    connectWithSpecNonce(link, 2, 0);
    putAt(link, &Link::writeAuth, 2, "01 01 05 b0 b1 b2 b3 b4 b5 b6 b7 4e 47 0a 94 b9 24 ac ae", 0);
    expectFrame("00 00 a0 a1 a2 a3 a4 a5 a6 a7", link.readAuth(2), "foreign proto");
    putAt(link, &Link::writeAuth, 2, "00 01 05 b0 b1 b2 b3 b4 b5 b6 b7 4e 47 0a 94 b9 24 ac", 0);
    expectFrame("00 00 a0 a1 a2 a3 a4 a5 a6 a7", link.readAuth(2), "short proof");
    putAt(link, &Link::writeAuth, 2, "00 01 05 b0 b1 b2 b3 b4 b5 b6 b7 4e 47 0a 94 b9 24 ac ae", 0);
    expectFrame("00 02 02", link.readAuth(2), "unknown slot");
    putAt(link, &Link::writeAuth, 2, "00 01 09 b0 b1 b2 b3 b4 b5 b6 b7 4e 47 0a 94 b9 24 ac ae", 0);
    expectFrame("00 02 02", link.readAuth(2), "still the first result");
}

void test_unauthenticated_timeout() {
    Link link(fakeFill, fakeWord);
    uint16_t due[Garmin::MAX_CONNECTIONS];

    connectWithSpecNonce(link, 3, 0);
    TEST_ASSERT_EQUAL(0, link.dueDisconnects(9999, due));
    TEST_ASSERT_EQUAL(1, link.dueDisconnects(10000, due));
    TEST_ASSERT_EQUAL(3, due[0]);
    link.disconnect(3);

    // A refused request does not restart it: an idle central must not hold a slot forever.
    connectWithSpecNonce(link, 4, 20000);
    putAt(link, &Link::writePairing, 4, "00 01 af 10", 25000);
    expectFrame("00 02 01", link.readPairing(4), "window closed");
    TEST_ASSERT_EQUAL(0, link.dueDisconnects(29999, due));
    TEST_ASSERT_EQUAL(1, link.dueDisconnects(30000, due));
    TEST_ASSERT_EQUAL(4, due[0]);
    link.disconnect(4);

    // A delivered key does (spec 9).
    g_word = 4271;
    link.openPairing(20000);
    connectWithSpecNonce(link, 4, 20000);
    putAt(link, &Link::writePairing, 4, "00 01 af 10", 25000);
    TEST_ASSERT_EQUAL(20, link.readPairing(4).len);
    TEST_ASSERT_EQUAL(0, link.dueDisconnects(34999, due));
    TEST_ASSERT_EQUAL(1, link.dueDisconnects(35000, due));
    TEST_ASSERT_EQUAL(4, due[0]);
    link.closePairing();

    // An authenticated connection never times out.
    loadSpecKeys(link);
    authenticate(link, 5, 40000);
    link.disconnect(4);
    TEST_ASSERT_EQUAL(0, link.dueDisconnects(100000, due));
}

void test_pairing_window() {
    Link link(fakeFill, fakeWord);
    loadSpecKeys(link);
    GarminData data;
    link.takeDirtyData(data);
    connectWithSpecNonce(link, 1, 0);
    expectFrame("00 00", link.readPairing(1), "before a request");

    putAt(link, &Link::writePairing, 1, "00 01 af 10", 0);
    expectFrame("00 02 01", link.readPairing(1), "window closed");

    g_word = 4271 + 10000 * 7;
    TEST_ASSERT_EQUAL(4271, link.openPairing(1000));
    bool open;
    uint16_t code;
    TEST_ASSERT_TRUE(link.takeAdvChanged(open, code));
    TEST_ASSERT_TRUE(open);
    TEST_ASSERT_EQUAL(4271, code);
    TEST_ASSERT_FALSE(link.takeAdvChanged(open, code));

    putAt(link, &Link::writePairing, 1, "00 01 b0 10", 2000);
    expectFrame("00 02 02", link.readPairing(1), "bad code");
    // Too short, wrong type: ignored, the last result stays.
    putAt(link, &Link::writePairing, 1, "00 01 af", 2000);
    putAt(link, &Link::writePairing, 1, "00 02 af 10", 2000);
    expectFrame("00 02 02", link.readPairing(1), "malformed requests ignored");

    // Two other watches first, so this key lands in slot 2 as in spec 16.3.
    link.forgetAll(2000);
    for (uint8_t i = 0; i < 2; i++) {
        const uint16_t other = static_cast<uint16_t>(2 + i);
        connectWithSpecNonce(link, other, 2000);
        g_fills.push_back(seq(static_cast<uint8_t>(0x80 + 16 * i), 16));
        putAt(link, &Link::writePairing, other, "00 01 af 10", 2000);
        TEST_ASSERT_EQUAL(20, link.readPairing(other).len);
    }
    g_fills.push_back(seq(0x00, 16));
    putAt(link, &Link::writePairing, 1, "00 01 af 10", 3000);
    expectFrame("00 02 00 02 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f", link.readPairing(1), "spec 16.3");
    TEST_ASSERT_EQUAL(3, link.pairedCount());
    TEST_ASSERT_TRUE(link.takeDirtyData(data));
    TEST_ASSERT_EQUAL(1, data.slots[2].used);
    TEST_ASSERT_EQUAL_HEX8(0x0f, data.slots[2].key[15]);

    // The new key authenticates on the same connection (spec 9 step 5).
    putAt(link, &Link::writeAuth, 1, SPEC_PROOF, 3000);
    expectFrame("00 02 00 18 57 e1 b4 a9 6e bf 5a", link.readAuth(1), "auth after pairing");

    // Expiry closes the window and the key is no longer readable.
    TEST_ASSERT_EQUAL(1000, link.pairingRemainingMs(120000));
    link.tick(120999);
    TEST_ASSERT_TRUE(link.pairingOpen());
    link.tick(121000);
    TEST_ASSERT_FALSE(link.pairingOpen());
    TEST_ASSERT_EQUAL(0, link.pairingCode());
    expectFrame("00 00", link.readPairing(1), "key wiped at close");
    TEST_ASSERT_TRUE(link.takeAdvChanged(open, code));
    TEST_ASSERT_FALSE(open);
    TEST_ASSERT_EQUAL(0, code);

    // A request just past the window is refused even before tick() runs.
    link.openPairing(200000);
    putAt(link, &Link::writePairing, 1, "00 01 af 10", 320000);
    expectFrame("00 02 01", link.readPairing(1), "late request");
}

void test_pairing_notify_only_to_subscriber() {
    Link link(fakeFill, fakeWord);
    g_word = 4271;
    link.openPairing(0);
    connectWithSpecNonce(link, 1, 0);
    connectWithSpecNonce(link, 2, 0);
    link.subscribe(2, Link::Sub::Pairing, true);

    putAt(link, &Link::writePairing, 1, "00 01 af 10", 0);
    Link::Notify n;
    TEST_ASSERT_FALSE(link.takePairingNotify(n));

    putAt(link, &Link::writePairing, 2, "00 01 af 10", 0);
    TEST_ASSERT_TRUE(link.takePairingNotify(n));
    TEST_ASSERT_EQUAL(2, n.handle);
    TEST_ASSERT_EQUAL(20, n.frame.len);
    TEST_ASSERT_FALSE(link.takePairingNotify(n));
}

void test_commands_dedupe_and_ack() {
    Link link(fakeFill, fakeWord);
    loadSpecKeys(link);
    authenticate(link, 1, 0);
    link.subscribe(1, Link::Sub::State, true);
    const WatchState s = menuState();
    Link::Push pushes[Garmin::MAX_CONNECTIONS];
    TEST_ASSERT_EQUAL(1, link.preparePushes(&s, 0, pushes));
    TEST_ASSERT_EQUAL(0, link.preparePushes(&s, 50, pushes));

    put(link, &Link::writeCommand, 1, "00 08 07 00");
    Link::Queued q;
    TEST_ASSERT_TRUE(link.takeCommand(q));
    TEST_ASSERT_EQUAL(1, q.handle);
    TEST_ASSERT_EQUAL(7, q.cmd.seq);
    TEST_ASSERT_TRUE(q.cmd.id == Garmin::CommandId::Score);
    TEST_ASSERT_FALSE(link.takeCommand(q));

    link.acknowledge(1, 7, Garmin::AckStatus::Applied);
    TEST_ASSERT_EQUAL(1, link.preparePushes(&s, 100, pushes));
    TEST_ASSERT_EQUAL(7, pushes[0].body[2]);
    TEST_ASSERT_EQUAL(0, pushes[0].body[3]);

    // Duplicate: not queued, ack unchanged, full re-push.
    put(link, &Link::writeCommand, 1, "00 08 07 00");
    TEST_ASSERT_FALSE(link.takeCommand(q));
    TEST_ASSERT_EQUAL(1, link.preparePushes(&s, 150, pushes));
    TEST_ASSERT_EQUAL(7, pushes[0].body[2]);

    // Malformed, unknown (0x0C is unused), foreign PROTO.
    put(link, &Link::writeCommand, 1, "00 08 08");
    TEST_ASSERT_EQUAL(1, link.preparePushes(&s, 200, pushes));
    TEST_ASSERT_EQUAL(8, pushes[0].body[2]);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Garmin::AckStatus::Malformed), pushes[0].body[3]);
    put(link, &Link::writeCommand, 1, "00 0c 09");
    TEST_ASSERT_EQUAL(1, link.preparePushes(&s, 250, pushes));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Garmin::AckStatus::UnknownCmd), pushes[0].body[3]);
    put(link, &Link::writeCommand, 1, "01 08 0a 00");
    TEST_ASSERT_EQUAL(0, link.preparePushes(&s, 300, pushes));
    TEST_ASSERT_FALSE(link.takeCommand(q));

    // SYNC is answered here, never queued.
    put(link, &Link::writeCommand, 1, "00 0d 0b");
    TEST_ASSERT_FALSE(link.takeCommand(q));
    TEST_ASSERT_EQUAL(1, link.preparePushes(&s, 350, pushes));
    TEST_ASSERT_EQUAL(11, pushes[0].body[2]);
    TEST_ASSERT_EQUAL(0, pushes[0].body[3]);
}

void test_queue_full_busy_and_disconnect() {
    Link link(fakeFill, fakeWord);
    loadSpecKeys(link);
    authenticate(link, 1, 0);
    authenticate(link, 2, 0);

    for (uint8_t i = 1; i <= Link::QUEUE_DEPTH; i++) {
        const std::string f = strf("00 06 %02x", i);
        put(link, &Link::writeCommand, 1, f.c_str());
    }
    put(link, &Link::writeCommand, 2, "00 06 01");
    link.subscribe(2, Link::Sub::State, true);
    Link::Push pushes[Garmin::MAX_CONNECTIONS];
    TEST_ASSERT_EQUAL(1, link.preparePushes(nullptr, 0, pushes));
    TEST_ASSERT_EQUAL(2, pushes[0].handle);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Garmin::AckStatus::Busy), pushes[0].body[3]);

    // A connection that has gone takes its queued commands with it.
    link.disconnect(1);
    Link::Queued q;
    TEST_ASSERT_FALSE(link.takeCommand(q));

    // Under an overlay every queued command is answered BUSY.
    put(link, &Link::writeCommand, 2, "00 06 02");
    put(link, &Link::writeCommand, 2, "00 06 03");
    TEST_ASSERT_EQUAL(2, link.rejectQueued(Garmin::AckStatus::Busy));
    TEST_ASSERT_FALSE(link.takeCommand(q));
    TEST_ASSERT_EQUAL(1, link.preparePushes(nullptr, 50, pushes));
    TEST_ASSERT_EQUAL(3, pushes[0].body[2]);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Garmin::AckStatus::Busy), pushes[0].body[3]);
}

void test_push_spec_vector_and_retry() {
    Link link(fakeFill, fakeWord);
    loadSpecKeys(link);
    loadSpecRoster(link);
    TEST_ASSERT_EQUAL_HEX32(0x150fe75b, link.rosterVersion());
    authenticate(link, 1, 0);

    // No subscription, no push.
    const WatchState s = menuState();
    Link::Push pushes[Garmin::MAX_CONNECTIONS];
    TEST_ASSERT_EQUAL(0, link.preparePushes(&s, 0, pushes));

    link.subscribe(1, Link::Sub::State, true);
    TEST_ASSERT_EQUAL(1, link.preparePushes(&s, 0, pushes));
    TEST_ASSERT_EQUAL(1, Garmin::chunkCount(pushes[0].len));
    expectFrame("00 01 01 01 00 00 00 5b e7 0f 15 04 04 04 01 02 03", chunk(pushes[0], 0), "spec 16.5");
    // The read returns chunk 0 of the same state.
    expectFrame("00 01 01 01 00 00 00 5b e7 0f 15 04 04 04 01 02 03", link.readState(1), "state read");

    // A failed chunk: nothing until the retry delay, then a whole set under a new stateSeq.
    link.pushFailed(1, 1000);
    TEST_ASSERT_EQUAL(1, link.failedPushes());
    TEST_ASSERT_EQUAL(0, link.preparePushes(&s, 1000 + Link::PUSH_RETRY_MS - 1, pushes));
    TEST_ASSERT_EQUAL(1, link.preparePushes(&s, 1000 + Link::PUSH_RETRY_MS, pushes));
    TEST_ASSERT_EQUAL(2, pushes[0].stateSeq);

    // A changed state pushes; an identical one does not.
    WatchState choose;
    choose.setChoosePlayers(0x5);
    choose.setSport(Garmin::SportId::Squash);
    TEST_ASSERT_EQUAL(1, link.preparePushes(&choose, 2000, pushes));
    TEST_ASSERT_EQUAL(0x10, pushes[0].body[0]);
    TEST_ASSERT_EQUAL(0x01, pushes[0].body[1]);
    TEST_ASSERT_EQUAL(0, link.preparePushes(&choose, 2050, pushes));

    // Unsubscribed: silent.
    link.subscribe(1, Link::Sub::State, false);
    TEST_ASSERT_EQUAL(0, link.preparePushes(&s, 3000, pushes));
}

void test_roster_reads() {
    Link link(fakeFill, fakeWord);
    loadSpecKeys(link);
    loadSpecRoster(link);
    authenticate(link, 1, 0);

    put(link, &Link::writeRoster, 1, "00 00");
    expectFrame("00 00 03 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00", link.readRoster(1), "entry 0");
    expectFrame("00 01 03 44 33 22 11 00 40 ff 4b 52 59 53 54 49 41 4e 00", link.readRoster(1), "entry 1");
    expectFrame("00 02 03 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00", link.readRoster(1), "entry 2");
    expectFrame("00 03 03", link.readRoster(1), "past the end");
    expectFrame("00 03 03", link.readRoster(1), "stays past the end");
    put(link, &Link::writeRoster, 1, "00 02");
    expectFrame("00 02 03 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00", link.readRoster(1), "cursor set");
    put(link, &Link::writeRoster, 1, "00");
    expectFrame("00 03 03", link.readRoster(1), "short cursor write ignored");
}

// One key per connection, and a re-issued slot drops the live connection that proved its old key.
void test_pairing_one_key_per_connection_and_rekey() {
    Link link(fakeFill, fakeWord);
    loadSpecKeys(link);
    authenticate(link, 1, 0); // slot 2 (spec key), now the most recently used of slots 0-2
    g_word = 4271;
    link.openPairing(0);

    connectWithSpecNonce(link, 2, 0);
    g_fills.push_back(seq(0x40, 16));
    putAt(link, &Link::writePairing, 2, "00 01 af 10", 100);
    const Garmin::Frame first = link.readPairing(2);
    TEST_ASSERT_EQUAL(20, first.len);
    g_fills.push_back(seq(0x60, 16));
    putAt(link, &Link::writePairing, 2, "00 01 af 10", 200);
    expectFrame(dump(first.data, first.len).c_str(), link.readPairing(2), "second request ignored");
    TEST_ASSERT_EQUAL(4, link.pairedCount());

    // Fill every slot from other connections until slot 2 (authenticated, oldest) is evicted.
    uint16_t due[Garmin::MAX_CONNECTIONS];
    TEST_ASSERT_EQUAL(0, link.dueDisconnects(300, due));
    uint16_t handle = 10;
    while (link.authedCount() == 1 && handle < 30) {
        link.disconnect(static_cast<uint16_t>(handle - 1));
        connectWithSpecNonce(link, handle, 300);
        putAt(link, &Link::writePairing, handle, "00 01 af 10", 300);
        handle++;
    }
    TEST_ASSERT_EQUAL(0, link.authedCount());
    TEST_ASSERT_EQUAL(1, link.dueDisconnects(300, due));
    TEST_ASSERT_EQUAL(1, due[0]);
}

void test_forget_all_drops_trust() {
    Link link(fakeFill, fakeWord);
    loadSpecKeys(link);
    authenticate(link, 1, 0);
    TEST_ASSERT_EQUAL(1, link.authedCount());

    link.forgetAll(5000);
    TEST_ASSERT_EQUAL(0, link.authedCount());
    TEST_ASSERT_EQUAL(0, link.pairedCount());
    uint16_t due[Garmin::MAX_CONNECTIONS];
    TEST_ASSERT_EQUAL(1, link.dueDisconnects(5000, due));
    put(link, &Link::writeCommand, 1, "00 06 01");
    Link::Queued q;
    TEST_ASSERT_FALSE(link.takeCommand(q));
    GarminData data;
    TEST_ASSERT_TRUE(link.takeDirtyData(data));
    TEST_ASSERT_EQUAL(0, GarminPairingStore::pairedCount(data));
    TEST_ASSERT_EQUAL_HEX32(0x12345678, data.boardId);
}

void test_connection_limit_and_enable() {
    Link link(fakeFill, fakeWord);
    for (uint16_t h = 0; h < Garmin::MAX_CONNECTIONS; h++) TEST_ASSERT_TRUE(link.connect(h, 0));
    TEST_ASSERT_FALSE(link.connect(10, 0));
    link.disconnect(2);
    TEST_ASSERT_TRUE(link.connect(10, 0));
    uint16_t handles[Garmin::MAX_CONNECTIONS];
    TEST_ASSERT_EQUAL(4, link.connectedHandles(handles));
    link.resetConnections();
    TEST_ASSERT_EQUAL(0, link.connectedHandles(handles));

    // The first enable draws the board id; disabling keeps it.
    TEST_ASSERT_FALSE(link.enabled());
    g_word = 0xCAFEF00D;
    link.setEnabled(true);
    TEST_ASSERT_TRUE(link.enabled());
    TEST_ASSERT_EQUAL_HEX32(0xCAFEF00D, link.boardId());
    g_word = 1;
    link.setEnabled(false);
    TEST_ASSERT_EQUAL_HEX32(0xCAFEF00D, link.boardId());
    GarminData data;
    TEST_ASSERT_TRUE(link.takeDirtyData(data));
    TEST_ASSERT_EQUAL(0, data.enabled);
    TEST_ASSERT_EQUAL_HEX32(0xCAFEF00D, data.boardId);
}

void test_watch_sport_table() {
    TEST_ASSERT_TRUE(WatchSport::fromModeState(DeviceModeState::SquashMode).sport == Garmin::SportId::Squash);
    TEST_ASSERT_TRUE(WatchSport::fromModeState(DeviceModeState::PadelMode).sport == Garmin::SportId::Padel);
    TEST_ASSERT_TRUE(WatchSport::fromModeState(DeviceModeState::ShortVolleyballMode).sport
                     == Garmin::SportId::ShortVolleyball);
    TEST_ASSERT_TRUE(WatchSport::fromModeState(DeviceModeState::VolleyballMode).viewDescribes);

    const WatchModeInfo menu = WatchSport::fromModeState(DeviceModeState::ModeSwitchingMode);
    TEST_ASSERT_TRUE(menu.sport == Garmin::SportId::None);
    TEST_ASSERT_TRUE(menu.viewDescribes);

    const WatchModeInfo config = WatchSport::fromModeState(DeviceModeState::ConfigMode);
    TEST_ASSERT_FALSE(config.viewDescribes);
    TEST_ASSERT_TRUE(config.screen == Garmin::ScreenId::Config);
    TEST_ASSERT_TRUE(WatchSport::fromModeState(DeviceModeState::PlayerSetupMode).screen == Garmin::ScreenId::Profile);
    TEST_ASSERT_TRUE(WatchSport::fromModeState(DeviceModeState::Booting).screen == Garmin::ScreenId::Booting);
    TEST_ASSERT_TRUE(WatchSport::fromModeState(static_cast<DeviceModeState>(99)).screen == Garmin::ScreenId::Booting);

    DeviceModeState mode;
    TEST_ASSERT_TRUE(WatchSport::modeForSport(0x02, mode));
    TEST_ASSERT_TRUE(mode == DeviceModeState::VolleyballMode);
    TEST_ASSERT_TRUE(WatchSport::modeForSport(0x04, mode));
    TEST_ASSERT_TRUE(mode == DeviceModeState::PadelMode);
    TEST_ASSERT_FALSE(WatchSport::modeForSport(0x00, mode));
    TEST_ASSERT_FALSE(WatchSport::modeForSport(0x05, mode));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_auth_spec_vector);
    RUN_TEST(test_unauthenticated_is_gated);
    RUN_TEST(test_proof_is_single_use);
    RUN_TEST(test_unauthenticated_timeout);
    RUN_TEST(test_pairing_window);
    RUN_TEST(test_pairing_notify_only_to_subscriber);
    RUN_TEST(test_commands_dedupe_and_ack);
    RUN_TEST(test_queue_full_busy_and_disconnect);
    RUN_TEST(test_push_spec_vector_and_retry);
    RUN_TEST(test_roster_reads);
    RUN_TEST(test_pairing_one_key_per_connection_and_rekey);
    RUN_TEST(test_forget_all_drops_trust);
    RUN_TEST(test_connection_limit_and_enable);
    RUN_TEST(test_watch_sport_table);
    return UNITY_END();
}
