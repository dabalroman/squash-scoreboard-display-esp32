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

    // --- Tournament sync (spec 20), loop() only ---

    // The running tournament's roster (count * 16 wire bytes) in its own slot; the stored ones stay.
    void setTournamentRoster(const uint8_t *entries, uint8_t count);
    // Like useRoster(): effective with the next published state.
    void useTournamentRoster();
    // BEGIN answering APPLIED or INVALID: entries 0..count-1 (16 B each) into `out`, stage cleared.
    void takeRosterStage(uint8_t count, uint8_t *out);
    // Leaving the tournament.
    void clearRosterStage();
    // Every publish: the STATUS / RECORD snapshot (all zero without a tournament). Never pushes;
    // cheap when nothing changed. `force` after a writer reset.
    void publishTournament(const Tournament *tournament, bool force = false);
    // A RECORD write waiting for loop(); recordApplied() releases its write response.
    bool takeRecord(uint32_t &tid, uint8_t &epoch, uint8_t &writer, uint8_t *image);
    void recordApplied();
    // Pairing slots written since the last call, as a bitmask.
    uint8_t takeRepairedSlots();

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
