#ifndef TABLET_SESSION_H
#define TABLET_SESSION_H

#include <stdint.h>
#include <string.h>
#include <memory>
#include <vector>

#include "Garmin/GarminProtocol.h"
#include "UserProfile.h"
#include "Utils.h"

/**
 * A tablet's tournament on this board (docs/garmin-protocol.md 20.2): its id, sport and the
 * pushed roster, RAM only. Profiles live from begin() to end() at fixed addresses (id = index),
 * so a re-push renames or recolours them in place under the modes' pointers. Pure, header-only;
 * the sport's menu check and the tid-vs-screen rules are the caller's.
 */
class TabletSession {
public:
    enum : uint8_t { MIN_ENTRIES = 2, MAX_ENTRIES = Garmin::TABLET_ROSTER_MAX };

    bool active() const {
        return tid != 0;
    }

    uint32_t tournamentId() const {
        return tid;
    }

    uint8_t sport() const {
        return sportId;
    }

    uint8_t count() const {
        return static_cast<uint8_t>(pointers.size());
    }

    uint32_t rosterCrc() const {
        return crc;
    }

    // count() * 16 wire bytes, what ROSTER reads serve.
    const uint8_t *wire() const {
        return table;
    }

    std::vector<UserProfile *> &profiles() {
        return pointers;
    }

    // Staged `entries` (count * 16 B) against BEGIN's count and CRC: count 2..16, non-zero
    // unique uids, printable NUL-padded names.
    static bool validRoster(const uint8_t count, const uint32_t rosterCrc, const uint8_t *entries) {
        if (count < MIN_ENTRIES || count > MAX_ENTRIES || entries == nullptr) return false;
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t *e = entries + static_cast<size_t>(i) * Garmin::ROSTER_ENTRY_SIZE;
            const uint32_t uid = Garmin::getU32(e);
            if (uid == 0 || !validName(e + 7)) return false;
            for (uint8_t j = 0; j < i; j++) {
                if (Garmin::getU32(entries + static_cast<size_t>(j) * Garmin::ROSTER_ENTRY_SIZE) == uid) return false;
            }
        }
        return Garmin::rosterVersion(count, entries) == rosterCrc;
    }

    // TOURNAMENT_WAIT's BEGIN. False (nothing changes) when one runs or anything is invalid.
    bool begin(const uint32_t tournamentId, const uint8_t sport, const uint8_t count, const uint32_t rosterCrc,
               const uint8_t *entries) {
        if (active() || tournamentId == 0 || sport == 0 || !validRoster(count, rosterCrc, entries)) return false;
        tid = tournamentId;
        sportId = sport;
        apply(count, rosterCrc, entries);
        return true;
    }

    /**
     * A re-push while the tournament runs: same id and sport, every current uid at its index,
     * new entries only appended. Games reference players by index, so a drop or a move is
     * refused. False = nothing changes; an identical roster is true and changes nothing.
     */
    bool replace(const uint32_t tournamentId, const uint8_t sport, const uint8_t count, const uint32_t rosterCrc,
                 const uint8_t *entries) {
        if (!active() || tournamentId != tid || sport != sportId || count < this->count()
            || !validRoster(count, rosterCrc, entries)) {
            return false;
        }
        for (uint8_t i = 0; i < this->count(); i++) {
            if (Garmin::getU32(entries + static_cast<size_t>(i) * Garmin::ROSTER_ENTRY_SIZE) != pointers[i]->getUid()) {
                return false;
            }
        }
        apply(count, rosterCrc, entries);
        return true;
    }

    void end() {
        tid = 0;
        sportId = 0;
        crc = 0;
        pointers.clear();
        storage.clear();
        memset(table, 0, sizeof(table));
    }

private:
    uint32_t tid = 0;
    uint8_t sportId = 0;
    uint32_t crc = 0;
    uint8_t table[MAX_ENTRIES * Garmin::ROSTER_ENTRY_SIZE] = {};
    std::vector<std::unique_ptr<UserProfile>> storage;
    std::vector<UserProfile *> pointers;

    // 1 to 9 printable ASCII characters, then NULs only; a 9-character name has no NUL.
    static bool validName(const uint8_t *name) {
        uint8_t n = 0;
        while (n < Garmin::ROSTER_NAME_SIZE && name[n] != 0) {
            if (name[n] < 0x20 || name[n] > 0x7E) return false;
            n++;
        }
        if (n == 0) return false;
        for (uint8_t i = n; i < Garmin::ROSTER_NAME_SIZE; i++) {
            if (name[i] != 0) return false;
        }
        return true;
    }

    // Existing profiles are overwritten in place, new ones appended.
    void apply(const uint8_t count, const uint32_t rosterCrc, const uint8_t *entries) {
        memcpy(table, entries, static_cast<size_t>(count) * Garmin::ROSTER_ENTRY_SIZE);
        crc = rosterCrc;
        storage.reserve(MAX_ENTRIES);
        pointers.reserve(MAX_ENTRIES);
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t *e = table + static_cast<size_t>(i) * Garmin::ROSTER_ENTRY_SIZE;
            char name[Garmin::ROSTER_NAME_SIZE + 1] = {};
            memcpy(name, e + 7, Garmin::ROSTER_NAME_SIZE);
            const UserProfile profile(i, Garmin::getU32(e), name, Color(e[4], e[5], e[6]));
            if (i < storage.size()) {
                *storage[i] = profile;
            } else {
                storage.push_back(std::make_unique<UserProfile>(profile));
                pointers.push_back(storage.back().get());
            }
        }
    }
};

#endif //TABLET_SESSION_H
