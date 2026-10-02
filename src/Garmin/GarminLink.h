#ifndef GARMIN_LINK_H
#define GARMIN_LINK_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "GarminData.h"
#include "GarminProtocol.h"
#include "Sha256.h"
#include "WatchCommand.h"
#include "WatchState.h"

/**
 * The board's protocol state (spec 6-12) without NimBLE: connection slots, auth, pairing,
 * dedupe, the command queue, acks, state pushes, timeouts and the in-RAM GarminData.
 * GarminService feeds it the host task's events and sends what it hands back, so the host
 * suites test the logic the board runs.
 *
 * Threading: host-task events and loop() calls meet here. Every public method holds `Guard`
 * (a portMUX spinlock on the device) for its whole body, except writeAuth(), which drops it
 * around the HMAC. Guarded sections only copy bytes: no allocation, no NVS, no NimBLE.
 */
template<class Guard>
class GarminLinkT {
public:
    typedef void (*RandomFill)(uint8_t *out, size_t len);

    enum : uint16_t { NO_HANDLE = 0xFFFF };

    enum : uint8_t {
        QUEUE_DEPTH = 8,
        MAX_BODY = Garmin::STATE_HEADER + WatchState::MAX_DATA,
    };

    enum : uint32_t {
        // A failed notify is retried this much later, as a full push under a new stateSeq.
        PUSH_RETRY_MS = 200,
        // A terminate that did not take is re-issued after this.
        TERMINATE_RETRY_MS = 1000,
    };

    enum class Sub : uint8_t { State, Pairing };

    struct Queued {
        uint16_t handle;
        WatchCommand cmd;
    };

    struct Push {
        uint16_t handle;
        uint8_t stateSeq;
        uint8_t len;
        uint8_t body[MAX_BODY];
    };

    struct Notify {
        uint16_t handle;
        Garmin::Frame frame;
    };

    GarminLinkT(const RandomFill fill, const GarminPairingStore::RandomSource randomWord)
        : fill(fill), randomWord(randomWord) {
        GarminPairingStore::fresh(data);
    }

    // --- Persistent data (setup / loop) ---

    // Returns whether the stored blob was accepted (else fresh: off, no watches, no board id).
    bool loadData(const uint8_t *blob, const size_t len) {
        Guard g;
        dataDirty = false;
        return GarminPairingStore::load(blob, len, data);
    }

    bool enabled() const {
        Guard g;
        return data.enabled != 0;
    }

    uint32_t boardId() const {
        Guard g;
        return data.boardId;
    }

    // The first enable draws the board id (spec 3).
    void setEnabled(const bool on) {
        Guard g;
        if (on) GarminPairingStore::ensureBoardId(data, randomWord);
        data.enabled = on ? 1 : 0;
        dataDirty = true;
    }

    // For a blob that was stored enabled but carries no usable id. True when it changed.
    bool ensureBoardId() {
        Guard g;
        const bool changed = GarminPairingStore::ensureBoardId(data, randomWord);
        if (changed) dataDirty = true;
        return changed;
    }

    // loop() persists what this hands out; NVS is never written from the host task.
    bool takeDirtyData(GarminData &out) {
        Guard g;
        if (!dataDirty) return false;
        out = data;
        dataDirty = false;
        return true;
    }

    void markDataDirty() {
        Guard g;
        dataDirty = true;
    }

    uint8_t pairedCount() const {
        Guard g;
        return GarminPairingStore::pairedCount(data);
    }

    // Every authenticated connection loses its trust and is dropped: its key is gone.
    void forgetAll(const uint32_t now) {
        Guard g;
        GarminPairingStore::forgetAll(data);
        dataDirty = true;
        for (uint8_t i = 0; i < Garmin::MAX_CONNECTIONS; i++) {
            Conn &c = conns[i];
            if (c.handle == NO_HANDLE) continue;
            wipePairingReply(c);
            if (!c.authed) continue;
            c.authed = false;
            c.dropPending = true;
            c.dropAt = now;
        }
    }

    // --- Roster (setup; it only changes across a reboot) ---

    void setRoster(uint8_t count, const uint8_t *entries) {
        if (count > ROSTER_MAX) count = ROSTER_MAX;
        // The CRC is the slow part; it runs before the lock.
        const uint32_t version = Garmin::rosterVersion(count, entries);
        Guard g;
        rosterCount = count;
        memcpy(roster, entries, static_cast<size_t>(count) * Garmin::ROSTER_ENTRY_SIZE);
        rosterVer = version;
    }

    uint32_t rosterVersion() const {
        Guard g;
        return rosterVer;
    }

    // --- Pairing window (loop) ---

    // A fresh code on every open, also when re-opened while open.
    uint16_t openPairing(const uint32_t now) {
        const uint16_t code = Garmin::pairingCodeFrom(randomWord());
        Guard g;
        pairing = true;
        pairingOpenedAt = now;
        pairingCode_ = code;
        advChanged = true;
        return code;
    }

    void closePairing() {
        Guard g;
        closePairingLocked();
    }

    bool pairingOpen() const {
        Guard g;
        return pairing;
    }

    uint16_t pairingCode() const {
        Guard g;
        return pairing ? pairingCode_ : 0;
    }

    uint32_t pairingRemainingMs(const uint32_t now) const {
        Guard g;
        if (!pairing) return 0;
        const uint32_t elapsed = now - pairingOpenedAt;
        return elapsed >= Garmin::PAIRING_WINDOW_MS ? 0 : Garmin::PAIRING_WINDOW_MS - elapsed;
    }

    // The advertising payload must follow the pairing flag and code.
    bool takeAdvChanged(bool &open, uint16_t &code) {
        Guard g;
        if (!advChanged) return false;
        advChanged = false;
        open = pairing;
        code = pairing ? pairingCode_ : 0;
        return true;
    }

    // --- Host task: connection events ---

    // False when every slot is taken (the caller terminates the connection).
    bool connect(const uint16_t handle, const uint32_t now) {
        uint8_t nb[Garmin::NONCE_SIZE];
        fill(nb, sizeof(nb));
        Guard g;
        Conn *c = find(handle);
        if (c == nullptr) c = find(NO_HANDLE);
        if (c == nullptr) return false;
        *c = Conn();
        c->handle = handle;
        memcpy(c->nb, nb, sizeof(nb));
        c->unauthSince = now;
        return true;
    }

    void disconnect(const uint16_t handle) {
        Guard g;
        Conn *c = find(handle);
        if (c == nullptr) return;
        wipePairingReply(*c);
        *c = Conn();
        for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
            if (queue[i].handle == handle) queue[i].handle = NO_HANDLE;
        }
    }

    void subscribe(const uint16_t handle, const Sub which, const bool on) {
        Guard g;
        Conn *c = find(handle);
        if (c == nullptr) return;
        if (which == Sub::Pairing) {
            c->pairingSub = on;
            return;
        }
        c->stateSub = on;
        // Spec 7 step 7: subscribing is what asks for the full current state.
        if (on) c->needPush = true;
    }

    // --- Host task: characteristic access ---

    Garmin::Frame readAuth(const uint16_t handle) {
        Guard g;
        const Conn *c = find(handle);
        if (c == nullptr) return Garmin::buildProtoOnly();
        return c->authReply.len ? c->authReply : Garmin::buildAuthChallenge(c->nb);
    }

    void writeAuth(const uint16_t handle, const uint8_t *in, const size_t len, const uint32_t now) {
        Garmin::AuthProof proof;
        if (!Garmin::parseAuthProof(in, len, proof)) return;

        uint8_t nb[Garmin::NONCE_SIZE];
        uint8_t key[Garmin::KEY_SIZE];
        bool haveKey;
        {
            Guard g;
            Conn *c = find(handle);
            // One proof per connection (spec 8): a second write is ignored.
            if (c == nullptr || c->proofUsed) return;
            c->proofUsed = true;
            memcpy(nb, c->nb, sizeof(nb));
            const uint8_t *stored = GarminPairingStore::keyFor(data, proof.slotId);
            haveKey = stored != nullptr;
            if (haveKey) memcpy(key, stored, sizeof(key));
        }

        // Outside the spinlock: an HMAC is far too long to hold interrupts off for.
        bool ok = haveKey && GarminAuth::verifyWatchMac(key, nb, proof.mac);
        uint8_t boardMac[Garmin::MAC_SIZE];
        if (ok) GarminAuth::boardMac(key, proof.nw, boardMac);

        {
            Guard g;
            Conn *c = find(handle);
            if (c != nullptr && c->proofUsed && c->authReply.len == 0) {
                // The slot may have been forgotten or re-issued while the MAC was computed.
                const uint8_t *stored = GarminPairingStore::keyFor(data, proof.slotId);
                if (ok && (stored == nullptr || !GarminAuth::constantTimeEquals(stored, key, sizeof(key)))) {
                    ok = false;
                    haveKey = false; // the key it proved is gone: re-pair, not "untrusted"
                }

                if (ok) {
                    c->authed = true;
                    c->authSlot = static_cast<int8_t>(proof.slotId);
                    c->authReply = Garmin::buildAuthResultOk(boardMac);
                    c->needPush = true;
                    GarminPairingStore::touch(data, proof.slotId);
                    dataDirty = true;
                } else {
                    c->authReply = Garmin::buildAuthResultFail(haveKey ? Garmin::AuthStatus::BadProof
                                                                       : Garmin::AuthStatus::UnknownSlot);
                    c->dropPending = true;
                    c->dropAt = now + Garmin::FAILED_PROOF_DISCONNECT_MS;
                }
            }
        }
        memset(key, 0, sizeof(key));
    }

    Garmin::Frame readPairing(const uint16_t handle) {
        Guard g;
        const Conn *c = find(handle);
        if (c == nullptr || c->pairingReply.len == 0) return Garmin::buildPairingNone();
        return c->pairingReply;
    }

    void writePairing(const uint16_t handle, const uint8_t *in, const size_t len, const uint32_t now) {
        uint16_t code;
        if (!Garmin::parsePairingRequest(in, len, code)) return;

        uint8_t key[Garmin::KEY_SIZE];
        fill(key, sizeof(key));
        {
            Guard g;
            Conn *c = find(handle);
            if (c != nullptr) {
                if (!pairing || now - pairingOpenedAt >= Garmin::PAIRING_WINDOW_MS) {
                    c->pairingReply = Garmin::buildPairingFail(Garmin::PairingStatus::WindowClosed);
                } else if (code != pairingCode_) {
                    c->pairingReply = Garmin::buildPairingFail(Garmin::PairingStatus::BadCode);
                } else if (c->paired) {
                    // One key per connection: a central must not mint keys and evict every slot.
                    return;
                } else {
                    const uint8_t slot = GarminPairingStore::storeKey(data, key);
                    dataDirty = true;
                    c->paired = true;
                    c->pairingReply = Garmin::buildPairingOk(slot, key);
                    // Spec 9: the unauthenticated timeout restarts when a key is delivered; a failed
                    // request must not let an idle central hold a connection slot forever.
                    c->unauthSince = now;
                    dropHoldersOf(slot, now, c);
                }
                c->pairingNotify = c->pairingSub;
            }
        }
        memset(key, 0, sizeof(key));
    }

    // Before authentication COMMAND writes are accepted and discarded (spec 6).
    void writeCommand(const uint16_t handle, const uint8_t *in, const size_t len) {
        WatchCommand cmd;
        const Garmin::CommandParse parse = Garmin::parseCommand(in, len, cmd);
        if (parse == Garmin::CommandParse::Drop) return;

        Guard g;
        Conn *c = find(handle);
        if (c == nullptr || !c->authed) return;

        // A duplicate recovers a lost ack: not applied, ack unchanged, full re-push (spec 10.3).
        if (cmd.seq == c->lastSeq) {
            c->needPush = true;
            return;
        }
        c->lastSeq = cmd.seq;

        if (parse != Garmin::CommandParse::Ok) {
            ack(*c, cmd.seq, Garmin::ackFor(parse));
        } else if (cmd.id == Garmin::CommandId::Sync) {
            ack(*c, cmd.seq, Garmin::AckStatus::Applied);
        } else if (!enqueue(handle, cmd)) {
            ack(*c, cmd.seq, Garmin::AckStatus::Busy);
        }
    }

    void writeRoster(const uint16_t handle, const uint8_t *in, const size_t len) {
        uint8_t index;
        if (!Garmin::parseRosterCursor(in, len, index)) return;
        Guard g;
        Conn *c = find(handle);
        if (c == nullptr || !c->authed) return;
        c->rosterCursor = index;
    }

    Garmin::Frame readRoster(const uint16_t handle) {
        Guard g;
        Conn *c = find(handle);
        if (c == nullptr || !c->authed) return Garmin::buildProtoOnly();
        const Garmin::Frame f = Garmin::buildRosterRead(c->rosterCursor, rosterCount, roster);
        if (c->rosterCursor < rosterCount) c->rosterCursor++;
        return f;
    }

    // Chunk 0 of the last published state, with this connection's ack (spec 11.1).
    Garmin::Frame readState(const uint16_t handle) {
        Guard g;
        const Conn *c = find(handle);
        if (c == nullptr || !c->authed) return Garmin::buildProtoOnly();
        uint8_t body[MAX_BODY];
        const uint8_t len = lastState.build(c->ackSeq, c->ackStatus, rosterVer, body);
        return Garmin::buildStateChunk(c->stateSeq, body, len, 0);
    }

    // --- loop() ---

    void tick(const uint32_t now) {
        Guard g;
        if (pairing && now - pairingOpenedAt >= Garmin::PAIRING_WINDOW_MS) closePairingLocked();
    }

    // Connections to terminate now: unauthenticated past the timeout, a failed proof's delay
    // over, or trust withdrawn by forgetAll().
    uint8_t dueDisconnects(const uint32_t now, uint16_t out[Garmin::MAX_CONNECTIONS]) {
        Guard g;
        uint8_t n = 0;
        for (uint8_t i = 0; i < Garmin::MAX_CONNECTIONS; i++) {
            Conn &c = conns[i];
            if (c.handle == NO_HANDLE) continue;
            const bool due = (c.dropPending && static_cast<int32_t>(now - c.dropAt) >= 0)
                             || (!c.authed && now - c.unauthSince >= Garmin::UNAUTH_TIMEOUT_MS);
            if (!due) continue;
            if (c.terminateSent && now - c.terminateAt < TERMINATE_RETRY_MS) continue;
            c.terminateSent = true;
            c.terminateAt = now;
            out[n++] = c.handle;
        }
        return n;
    }

    uint8_t connectedHandles(uint16_t out[Garmin::MAX_CONNECTIONS]) const {
        Guard g;
        uint8_t n = 0;
        for (uint8_t i = 0; i < Garmin::MAX_CONNECTIONS; i++) {
            if (conns[i].handle != NO_HANDLE) out[n++] = conns[i].handle;
        }
        return n;
    }

    // Oldest first; commands of a connection that has gone are skipped.
    bool takeCommand(Queued &out) {
        Guard g;
        return takeLocked(out);
    }

    void acknowledge(const uint16_t handle, const uint8_t seq, const Garmin::AckStatus status) {
        Guard g;
        Conn *c = find(handle);
        if (c != nullptr) ack(*c, seq, status);
    }

    // Overlay: nothing is applied, every queued command is answered BUSY.
    uint8_t rejectQueued(const Garmin::AckStatus status) {
        Guard g;
        uint8_t n = 0;
        Queued q;
        while (takeLocked(q)) {
            Conn *c = find(q.handle);
            if (c != nullptr) ack(*c, q.cmd.seq, status);
            n++;
        }
        return n;
    }

    uint8_t authedCount() const {
        Guard g;
        uint8_t n = 0;
        for (uint8_t i = 0; i < Garmin::MAX_CONNECTIONS; i++) {
            if (conns[i].handle != NO_HANDLE && conns[i].authed) n++;
        }
        return n;
    }

    /**
     * Bodies to notify: one per subscribed, authenticated connection whose state or ack
     * differs from what it was last sent, or that asked for a full push. `state` null
     * re-uses the last one (acks under an overlay). Each push takes a new stateSeq, so
     * after a failed chunk the watch discards the incomplete set.
     */
    uint8_t preparePushes(const WatchState *state, const uint32_t now, Push out[Garmin::MAX_CONNECTIONS]) {
        Guard g;
        if (state != nullptr) lastState = *state;
        uint8_t n = 0;
        for (uint8_t i = 0; i < Garmin::MAX_CONNECTIONS; i++) {
            Conn &c = conns[i];
            if (c.handle == NO_HANDLE || !c.authed || !c.stateSub) continue;
            if (c.retryPending && static_cast<int32_t>(now - c.retryAt) < 0) continue;
            const bool changed = c.needPush || !c.pushedOnce || c.pushed != lastState
                                 || c.pushedAckSeq != c.ackSeq || c.pushedAckStatus != c.ackStatus;
            if (!changed) continue;

            c.stateSeq++;
            c.needPush = false;
            c.retryPending = false;
            c.pushedOnce = true;
            c.pushed = lastState;
            c.pushedAckSeq = c.ackSeq;
            c.pushedAckStatus = c.ackStatus;

            Push &p = out[n++];
            p.handle = c.handle;
            p.stateSeq = c.stateSeq;
            p.len = lastState.build(c.ackSeq, c.ackStatus, rosterVer, p.body);
        }
        return n;
    }

    void pushFailed(const uint16_t handle, const uint32_t now) {
        Guard g;
        sendFailures++;
        Conn *c = find(handle);
        if (c == nullptr) return;
        c->needPush = true;
        c->retryPending = true;
        c->retryAt = now + PUSH_RETRY_MS;
    }

    uint32_t failedPushes() const {
        Guard g;
        return sendFailures;
    }

    // A pairing result for a connection subscribed to PAIRING (only ever its own).
    bool takePairingNotify(Notify &out) {
        Guard g;
        for (uint8_t i = 0; i < Garmin::MAX_CONNECTIONS; i++) {
            Conn &c = conns[i];
            if (c.handle == NO_HANDLE || !c.pairingNotify) continue;
            c.pairingNotify = false;
            if (!c.pairingSub || c.pairingReply.len == 0) continue;
            out.handle = c.handle;
            out.frame = c.pairingReply;
            return true;
        }
        return false;
    }

    // After the stack is torn down: no connection survives it.
    void resetConnections() {
        Guard g;
        for (uint8_t i = 0; i < Garmin::MAX_CONNECTIONS; i++) {
            wipePairingReply(conns[i]);
            conns[i] = Conn();
        }
        queueHead = 0;
        queueCount = 0;
        closePairingLocked();
    }

private:
    enum : uint8_t { ROSTER_MAX = 32 };

    struct Conn {
        uint16_t handle = NO_HANDLE;
        uint8_t nb[Garmin::NONCE_SIZE] = {};
        bool authed = false;
        bool proofUsed = false;
        bool stateSub = false;
        bool pairingSub = false;
        bool pairingNotify = false;
        bool needPush = false;
        bool pushedOnce = false;
        bool retryPending = false;
        bool dropPending = false;
        bool terminateSent = false;
        bool paired = false;
        // The slot this connection authenticated with; NO_SLOT before auth.
        int8_t authSlot = GarminPairingStore::NO_SLOT;
        Garmin::Frame authReply = {};
        Garmin::Frame pairingReply = {};
        uint8_t lastSeq = 0;
        uint8_t rosterCursor = 0;
        uint8_t ackSeq = 0;
        Garmin::AckStatus ackStatus = Garmin::AckStatus::Applied;
        uint8_t stateSeq = 0;
        WatchState pushed;
        uint8_t pushedAckSeq = 0;
        Garmin::AckStatus pushedAckStatus = Garmin::AckStatus::Applied;
        uint32_t unauthSince = 0;
        uint32_t dropAt = 0;
        uint32_t retryAt = 0;
        uint32_t terminateAt = 0;
    };

    // A re-issued slot no longer holds the key its live connection proved: drop that connection.
    void dropHoldersOf(const uint8_t slot, const uint32_t now, const Conn *except) {
        for (uint8_t i = 0; i < Garmin::MAX_CONNECTIONS; i++) {
            Conn &c = conns[i];
            if (&c == except || c.handle == NO_HANDLE || !c.authed || c.authSlot != slot) continue;
            c.authed = false;
            c.dropPending = true;
            c.dropAt = now;
        }
    }

    RandomFill fill;
    GarminPairingStore::RandomSource randomWord;

    GarminData data;
    bool dataDirty = false;

    Conn conns[Garmin::MAX_CONNECTIONS];

    Queued queue[QUEUE_DEPTH] = {};
    uint8_t queueHead = 0;
    uint8_t queueCount = 0;

    uint8_t roster[ROSTER_MAX * Garmin::ROSTER_ENTRY_SIZE] = {};
    uint8_t rosterCount = 0;
    uint32_t rosterVer = Garmin::rosterVersion(0, nullptr);

    bool pairing = false;
    uint32_t pairingOpenedAt = 0;
    uint16_t pairingCode_ = 0;
    bool advChanged = false;

    WatchState lastState;
    uint32_t sendFailures = 0;

    Conn *find(const uint16_t handle) {
        for (uint8_t i = 0; i < Garmin::MAX_CONNECTIONS; i++) {
            if (conns[i].handle == handle) return &conns[i];
        }
        return nullptr;
    }

    const Conn *find(const uint16_t handle) const {
        for (uint8_t i = 0; i < Garmin::MAX_CONNECTIONS; i++) {
            if (conns[i].handle == handle) return &conns[i];
        }
        return nullptr;
    }

    static void ack(Conn &c, const uint8_t seq, const Garmin::AckStatus status) {
        c.ackSeq = seq;
        c.ackStatus = status;
        // Also when the ack repeats an earlier one: every command answers with a push.
        c.needPush = true;
    }

    bool enqueue(const uint16_t handle, const WatchCommand &cmd) {
        if (queueCount >= QUEUE_DEPTH) return false;
        Queued &q = queue[(queueHead + queueCount) % QUEUE_DEPTH];
        q.handle = handle;
        q.cmd = cmd;
        queueCount++;
        return true;
    }

    bool takeLocked(Queued &out) {
        while (queueCount > 0) {
            const Queued q = queue[queueHead];
            queueHead = static_cast<uint8_t>((queueHead + 1) % QUEUE_DEPTH);
            queueCount--;
            if (q.handle == NO_HANDLE) continue;
            const Conn *c = find(q.handle);
            if (c == nullptr || !c->authed) continue;
            out = q;
            return true;
        }
        return false;
    }

    // A key is never readable outside the window (spec 9).
    static void wipePairingReply(Conn &c) {
        memset(&c.pairingReply, 0, sizeof(c.pairingReply));
    }

    void closePairingLocked() {
        if (!pairing) return;
        pairing = false;
        advChanged = true;
        for (uint8_t i = 0; i < Garmin::MAX_CONNECTIONS; i++) {
            if (conns[i].pairingReply.len > 2
                && conns[i].pairingReply.data[2] == static_cast<uint8_t>(Garmin::PairingStatus::Ok)) {
                wipePairingReply(conns[i]);
            }
        }
    }
};

#endif //GARMIN_LINK_H
