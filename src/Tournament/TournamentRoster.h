#ifndef TOURNAMENT_ROSTER_H
#define TOURNAMENT_ROSTER_H

#include <stdint.h>
#include <string.h>
#include <memory>
#include <vector>

#include "Garmin/GarminProtocol.h"
#include "UserProfile.h"
#include "Utils.h"

/**
 * A tournament's roster (docs/garmin-protocol.md 20.3), RAM only: copies of the picked stored
 * profiles or a peer's pushed entries, uids kept. Profiles stay at fixed addresses until clear(),
 * so a growth renames or recolours them in place under the matches' references.
 *
 * The first committedCount() entries are the roster (STATUS, roster_crc); entries past it are
 * the picker's selection, not started yet. The roster only grows.
 */
class TournamentRoster {
public:
    enum : uint8_t { MIN_ENTRIES = 2, MAX_ENTRIES = Garmin::TOURNAMENT_ROSTER_MAX };

    // Staged `entries` (count * 16 B) against BEGIN's count and CRC: count 2..16, non-zero
    // unique uids, printable NUL-padded names.
    static bool valid(const uint8_t count, const uint32_t rosterCrc, const uint8_t *entries) {
        if (count < MIN_ENTRIES || count > MAX_ENTRIES || entries == nullptr) return false;
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t *e = entryAt(entries, i);
            const uint32_t uid = Garmin::getU32(e);
            if (uid == 0 || !validName(e + 7)) return false;
            for (uint8_t j = 0; j < i; j++) {
                if (Garmin::getU32(entryAt(entries, j)) == uid) return false;
            }
        }
        return Garmin::rosterVersion(count, entries) == rosterCrc;
    }

    uint8_t size() const {
        return static_cast<uint8_t>(pointers.size());
    }

    uint8_t committedCount() const {
        return committed;
    }

    // committedCount() * 16 wire bytes, what ROSTER reads serve.
    const uint8_t *wire() const {
        return table;
    }

    uint32_t crc() const {
        return Garmin::rosterVersion(committed, table);
    }

    std::vector<UserProfile *> &profiles() {
        return pointers;
    }

    const std::vector<UserProfile *> &profiles() const {
        return pointers;
    }

    UserProfile *byUid(const uint32_t uid) const {
        for (UserProfile *p: pointers) {
            if (p->getUid() == uid) return p;
        }
        return nullptr;
    }

    // In the started roster, not only selected.
    UserProfile *committedByUid(const uint32_t uid) const {
        for (uint8_t i = 0; i < committed; i++) {
            if (pointers[i]->getUid() == uid) return pointers[i];
        }
        return nullptr;
    }

    // A stored profile picked: copied, its id kept when free. False when full.
    bool select(const UserProfile &profile) {
        if (byUid(profile.getUid()) != nullptr) return true;
        if (size() >= MAX_ENTRIES) return false;
        const uint8_t id = idInUse(profile.getId()) ? freeId() : profile.getId();
        append(UserProfile(id, profile.getUid(), profile.getName(), profile.getColor()));
        rebuildWire();
        return true;
    }

    // Only a selection not started yet can be un-selected.
    bool unselect(const uint32_t uid) {
        for (uint8_t i = committed; i < size(); i++) {
            if (pointers[i]->getUid() != uid) continue;
            release(pointers[i]);
            pointers.erase(pointers.begin() + i);
            return true;
        }
        return false;
    }

    // START: the selection becomes the roster.
    void commit() {
        committed = size();
        rebuildWire();
    }

    /**
     * BEGIN on MENU (empty roster) or roster growth: the staged list becomes the roster in its
     * order, names and colours; a selection not started yet stays after it while there is room.
     * False (nothing changes) when invalid or a started uid is missing.
     */
    bool adopt(const uint8_t count, const uint32_t rosterCrc, const uint8_t *entries) {
        if (!valid(count, rosterCrc, entries) || count < committed) return false;
        for (uint8_t i = 0; i < committed; i++) {
            if (!stagedHas(count, entries, pointers[i]->getUid())) return false;
        }
        std::vector<UserProfile *> next;
        next.reserve(MAX_ENTRIES);
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t *e = entryAt(entries, i);
            const uint32_t uid = Garmin::getU32(e);
            char name[Garmin::ROSTER_NAME_SIZE + 1] = {};
            memcpy(name, e + 7, Garmin::ROSTER_NAME_SIZE);
            const Color color(e[4], e[5], e[6]);
            UserProfile *existing = byUid(uid);
            if (existing != nullptr) {
                *existing = UserProfile(existing->getId(), uid, name, color);
                next.push_back(existing);
            } else {
                storage.push_back(std::make_unique<UserProfile>(UserProfile(nextFreeId(next), uid, name, color)));
                next.push_back(storage.back().get());
            }
        }
        for (uint8_t i = committed; i < size(); i++) {
            if (stagedHas(count, entries, pointers[i]->getUid())) continue;
            if (next.size() < MAX_ENTRIES) next.push_back(pointers[i]);
            else release(pointers[i]);
        }
        pointers = next;
        committed = count;
        rebuildWire();
        return true;
    }

    void clear() {
        committed = 0;
        pointers.clear();
        storage.clear();
        memset(table, 0, sizeof(table));
    }

private:
    uint8_t committed = 0;
    uint8_t table[MAX_ENTRIES * Garmin::ROSTER_ENTRY_SIZE] = {};
    // Owns the profiles `pointers` lists, so their addresses stay fixed while matches hold them.
    std::vector<std::unique_ptr<UserProfile>> storage;
    std::vector<UserProfile *> pointers;

    static const uint8_t *entryAt(const uint8_t *entries, const uint8_t i) {
        return entries + static_cast<size_t>(i) * Garmin::ROSTER_ENTRY_SIZE;
    }

    static bool stagedHas(const uint8_t count, const uint8_t *entries, const uint32_t uid) {
        for (uint8_t i = 0; i < count; i++) {
            if (Garmin::getU32(entryAt(entries, i)) == uid) return true;
        }
        return false;
    }

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

    bool idInUse(const uint8_t id) const {
        for (const UserProfile *p: pointers) {
            if (p->getId() == id) return true;
        }
        return false;
    }

    uint8_t freeId() const {
        uint8_t id = 0;
        while (idInUse(id)) id++;
        return id;
    }

    // Lowest id used neither in `next` nor by a current entry.
    uint8_t nextFreeId(const std::vector<UserProfile *> &next) const {
        for (uint8_t id = 0;; id++) {
            bool used = idInUse(id);
            for (const UserProfile *p: next) used = used || p->getId() == id;
            if (!used) return id;
        }
    }

    // Only a selection not started yet: no match references it.
    void release(const UserProfile *profile) {
        for (size_t i = 0; i < storage.size(); i++) {
            if (storage[i].get() != profile) continue;
            storage.erase(storage.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }

    void append(const UserProfile &profile) {
        storage.push_back(std::make_unique<UserProfile>(profile));
        pointers.push_back(storage.back().get());
    }

    void rebuildWire() {
        memset(table, 0, sizeof(table));
        for (uint8_t i = 0; i < size(); i++) {
            const UserProfile &p = *pointers[i];
            const Color c = p.getColor();
            Garmin::buildRosterEntry(p.getUid(), c.r, c.g, c.b, p.getName(), table + static_cast<size_t>(i) * Garmin::ROSTER_ENTRY_SIZE);
        }
    }
};

#endif //TOURNAMENT_ROSTER_H
