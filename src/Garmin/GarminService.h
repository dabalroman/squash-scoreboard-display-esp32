#ifndef GARMIN_SERVICE_H
#define GARMIN_SERVICE_H

#include <stdint.h>
#include <vector>

#include "UserProfile.h"
#include "WatchCommand.h"
#include "WatchState.h"

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

    // setup(), before begin(). The roster only changes across a reboot (the web save restarts).
    void setRoster(const std::vector<UserProfile *> &profiles);

    bool isEnabled() const;

    // Stores the flag (persisted on the next loop pass) and starts or stops BLE live.
    void setEnabled(bool on);

    void begin();
    void end();

    // NimBLE is up and advertising; every other hook is a no-op otherwise.
    bool running() const;

    // Every loop() pass: init/deinit outcome, NVS write, pairing expiry, timeouts.
    void loop(uint32_t now);

    // One queued watch command, oldest first.
    bool takeCommand(uint16_t &handle, WatchCommand &command);
    void acknowledge(uint16_t handle, uint8_t seq, Garmin::AckStatus status);

    // While an Overlay owns the displays: every queued command is answered BUSY.
    void rejectQueued();

    // Once per 50 ms tick: notifies every subscribed watch whose state or ack changed.
    void publish(const WatchState &state, uint32_t now);
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
