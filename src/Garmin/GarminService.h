#ifndef GARMIN_SERVICE_H
#define GARMIN_SERVICE_H

#include <stdint.h>
#include <vector>

#include "UserProfile.h"
#include "WatchCommand.h"
#include "WatchState.h"

class Tournament;

/**
 * Garmin App Remote BLE peripheral (docs/garmin-protocol.md), the only NimBLE user. One
 * instance, at file scope in main.cpp. Board-agnostic.
 *
 * Never fatal and never blocking: begin() and end() hand NimBLE init/deinit to one-shot
 * tasks on core 0 and return at once; loop() observes the outcome and logs it. A failure
 * leaves the feature off; boot, WiFi, OTA and telnet never wait on it.
 *
 * Cores: NimBLE's host task (core 0) runs the GATT callbacks against GarminLink under a
 * spinlock; everything else here is called from loop() (core 1), which also sends every
 * notification. NVS is written only from loop().
 */
class GarminService {
public:
    // setup(), before begin(): reads the "gar" blob.
    void load();

    // setup(), before begin(), once per kind. A roster only changes across a reboot (the web save restarts).
    void setRoster(Garmin::RosterKind kind, const std::vector<UserProfile *> &profiles);

    // Every mode build (spec 12): the roster the new mode's uids belong to. Cheap, no CRC.
    void useRoster(Garmin::RosterKind kind);

    // --- Tablet tournament (spec 20), loop() only ---

    // The pushed roster (count * 16 wire bytes) in its own slot; the stored ones stay. Returns its CRC.
    uint32_t setTabletRoster(const uint8_t *entries, uint8_t count);
    // Like useRoster(): effective with the next published state.
    void useTabletRoster();
    // On BEGIN_TOURNAMENT / COMMIT_MIRROR answering APPLIED or INVALID: copies entries
    // 0..count-1 (16 B / 15 B each) into `out` and clears that stage.
    void takeRosterStage(uint8_t count, uint8_t *out);
    void takeMirrorStage(uint8_t count, uint8_t *out);
    // Tablet mode ended: stages, pushed roster and snapshot cleared.
    void endTablet();

    // Writes the i-th un-ACKed board game (seq order) as a 15 B game entry, and its seq.
    typedef void (*OutboxAt)(const void *context, uint16_t i, uint16_t &seq, uint8_t *out);
    // Every publish: the STATUS / OUTBOX snapshot (all zero, count 0 outside tablet mode). Never pushes.
    void publishTablet(const Garmin::TabletStatus &status, uint16_t outboxCount, OutboxAt at, const void *context);

    bool isEnabled() const;

    // Stores the flag (persisted on the next loop pass) and starts or stops BLE live.
    void setEnabled(bool on);

    void begin();
    void end();

    // NimBLE is up and advertising; every other hook is a no-op otherwise.
    bool running() const;

    // Init failed (low heap, or blocked until a power cycle): enabling again will not help.
    bool failed() const;

    // Every loop() pass: init/deinit outcome, NVS write, pairing expiry, timeouts.
    void loop(uint32_t now);

    // One queued watch command, oldest first.
    bool takeCommand(uint16_t &handle, WatchCommand &command);
    void acknowledge(uint16_t handle, uint8_t seq, Garmin::AckStatus status);

    // While an Overlay owns the displays: every queued command is answered BUSY.
    void rejectQueued();

    // Once per 50 ms tick: notifies every subscribed watch whose state or ack changed, and HEART (spec 18).
    void publish(const WatchState &state, uint32_t now);
    // Every publish: mirrors the listed games for RESULTS reads (spec 19); null outside the sport modes.
    // Never pushes; cheap when nothing changed.
    void publishResults(const Tournament *tournament);
    // The last state again, for acks made while nothing is described (overlay).
    void publishAcks(uint32_t now);

    uint16_t openPairing();
    void closePairing();
    bool pairingOpen() const;
    uint16_t pairingCode() const;
    uint32_t pairingRemainingMs() const;

    uint8_t authedCount() const;
    // Bumped when a watch that paired authenticates with its new key.
    uint32_t completedPairings() const;
    uint8_t pairedCount() const;
    void forgetAll();

private:
    uint8_t lastLoggedState = 0;
    bool stallLogged = false;
    bool restartPending = false;
    bool persistBackoff = false;
    uint32_t persistFailedAt = 0;
    uint32_t loggedFailures = 0;
    uint32_t failureLoggedAt = 0;

    void sendPushes(const WatchState *state, uint32_t now);
};

#endif //GARMIN_SERVICE_H
