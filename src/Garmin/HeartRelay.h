#ifndef GARMIN_HEART_RELAY_H
#define GARMIN_HEART_RELAY_H

#include <stdint.h>
#include <string.h>

#include "GarminProtocol.h"

/**
 * HEART relay (spec 18), per connection slot: the latest bpm a watch wrote and what each
 * subscriber was last sent. Pure, clock injected; GarminLinkT runs it under its lock and decides
 * which slots count (authenticated sources, authenticated + subscribed receivers). RAM only.
 */
class HeartRelay {
public:
    enum : uint8_t { SLOTS = Garmin::MAX_CONNECTIONS };

    // 0 clears the slot's value.
    void set(const uint8_t slot, const uint8_t bpm, const uint32_t now) {
        if (slot >= SLOTS) return;
        slots[slot].bpm = bpm;
        slots[slot].at = now;
    }

    // Leaving PLAYING: every value goes, the receivers' sent lists stay so the clear is notified.
    void clearValues() {
        for (uint8_t i = 0; i < SLOTS; i++) slots[i].bpm = 0;
    }

    // A connection gone (or a new one in the slot): nothing of it survives.
    void reset(const uint8_t slot) {
        if (slot < SLOTS) slots[slot] = Slot();
    }

    // A fresh subscription counts as having been sent all zeros: a non-empty list goes out at
    // once (inside the rate limit), an empty one is not sent.
    void subscribed(const uint8_t slot) {
        if (slot >= SLOTS) return;
        memset(slots[slot].sent, 0, sizeof(slots[slot].sent));
        slots[slot].resend = false;
    }

    // `sources`: bit i = slot i may contribute. The receiver's own slot never does. Non-zero
    // values in slot order, padded with 0.
    void listFor(const uint8_t receiver, const uint8_t sources, const uint32_t now,
                 uint8_t out[Garmin::HEART_LIST]) {
        expire(now);
        memset(out, 0, Garmin::HEART_LIST);
        uint8_t n = 0;
        for (uint8_t i = 0; i < SLOTS && n < Garmin::HEART_LIST; i++) {
            if (i == receiver || (sources & (1u << i)) == 0 || slots[i].bpm == 0) continue;
            out[n++] = slots[i].bpm;
        }
    }

    // True = notify `receiver` with `out` now; it is then recorded as sent. At most one per
    // HEART_MIN_INTERVAL_MS; a change inside the window waits for its end and goes out with the
    // content current then, or not at all if that equals what was last sent.
    bool due(const uint8_t receiver, const uint8_t sources, const uint32_t now, uint8_t out[Garmin::HEART_LIST]) {
        if (receiver >= SLOTS) return false;
        listFor(receiver, sources, now, out);
        Slot &r = slots[receiver];
        if (!r.resend && memcmp(out, r.sent, Garmin::HEART_LIST) == 0) return false;
        if (r.sentOnce && now - r.sentAt < Garmin::HEART_MIN_INTERVAL_MS) return false;
        memcpy(r.sent, out, Garmin::HEART_LIST);
        r.sentOnce = true;
        r.sentAt = now;
        r.resend = false;
        return true;
    }

    // The notify did not go out: what the watch holds is unknown, so the next window sends again.
    void sendFailed(const uint8_t slot) {
        if (slot < SLOTS) slots[slot].resend = true;
    }

    uint8_t bpmOf(const uint8_t slot, const uint32_t now) {
        if (slot >= SLOTS) return 0;
        expire(now);
        return slots[slot].bpm;
    }

private:
    struct Slot {
        uint8_t bpm = 0;
        uint32_t at = 0;
        uint8_t sent[Garmin::HEART_LIST] = {};
        bool sentOnce = false;
        bool resend = false;
        uint32_t sentAt = 0;
    };

    Slot slots[SLOTS];

    // A host-task write can carry a later timestamp than loop()'s `now`: signed, so it never expires early.
    void expire(const uint32_t now) {
        for (uint8_t i = 0; i < SLOTS; i++) {
            Slot &s = slots[i];
            if (s.bpm != 0 && static_cast<int32_t>(now - s.at) >= static_cast<int32_t>(Garmin::HEART_EXPIRY_MS)) {
                s.bpm = 0;
            }
        }
    }
};

#endif //GARMIN_HEART_RELAY_H
