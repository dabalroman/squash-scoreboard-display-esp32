#ifndef TOURNAMENT_LOG_H
#define TOURNAMENT_LOG_H

#include <stdint.h>
#include <vector>
#include <esp_random.h>

#include "Garmin/GarminProtocol.h"
#include "Tournament/Game/GameRecord.h"

/**
 * A tournament's game records and change log (docs/garmin-protocol.md 20.2, 20.4). RAM only.
 * Records are kept sorted by (order, id); live ones are the RESULTS view (section 19).
 */
class TournamentLog {
public:
    enum : uint16_t { LIVE_MAX = Garmin::RESULTS_MAX, TOMBSTONE_MAX = Garmin::TOMBSTONE_MAX, SEQ_MAX = 0xFFFF };
    // Writer of a change made on the board or the fob; pairing slots are 0..SLOT_COUNT - 1.
    enum : uint8_t { BOARD = 0xFF };

    enum class Merge : uint8_t { Stored, Replaced, NoOp, Dropped };

    struct Entry {
        GameRecord record;
        uint16_t seq;
        uint8_t writer;
    };

    // A fresh, empty change log under `epoch` (non-zero).
    void start(const uint8_t epoch) {
        clear();
        // One allocation up front: a live insert never reallocates mid-game.
        entries.reserve(LIVE_MAX + TOMBSTONE_MAX + 1);
        logEpoch = epoch;
        rev = static_cast<uint16_t>(epoch << 8);
    }

    void clear() {
        entries.clear();
        changesTo = 0;
        ackedTo = 0;
        armed = false;
        lostTo = 0;
        logEpoch = 0;
        rev = 0;
        reserved = false;
    }

    uint8_t epoch() const {
        return logEpoch;
    }

    uint16_t lastSeq() const {
        return changesTo;
    }

    uint16_t lostSeq() const {
        return lostTo;
    }

    // RESULTS rev: changes on every change to the live records other than an append.
    uint16_t resultsRev() const {
        return rev;
    }

    size_t storedCount() const {
        return entries.size();
    }

    const Entry &stored(const size_t sortIndex) const {
        return entries[sortIndex];
    }

    size_t liveCount() const {
        size_t n = 0;
        for (const Entry &e: entries) {
            if (!e.record.deleted()) n++;
        }
        return n;
    }

    // `index` < liveCount(); the RESULTS view's index-th record. Returns its sort index.
    size_t liveAt(const size_t index) const {
        size_t live = 0;
        for (size_t i = 0; i < entries.size(); i++) {
            if (entries[i].record.deleted()) continue;
            if (live++ == index) return i;
        }
        return entries.size();
    }

    // -1 when no stored record has the id.
    int find(const uint32_t id) const {
        for (size_t i = 0; i < entries.size(); i++) {
            if (entries[i].record.id == id) return static_cast<int>(i);
        }
        return -1;
    }

    // --- The match slot: from START_MATCH until the board game's record exists ---

    bool isReserved() const {
        return reserved;
    }

    void setReserved(const bool on) {
        reserved = on;
    }

    // A reserved slot also keeps the last change seq for the board game.
    size_t liveCap() const {
        return reserved ? LIVE_MAX - 1 : LIVE_MAX;
    }

    bool full() const {
        return changesTo >= (reserved ? SEQ_MAX - 1 : SEQ_MAX);
    }

    // --- Writes ---

    /**
     * A record created here (a board game, ADD_OFFLINE_GAME): fresh id, rev 0, order above
     * everything stored. The caller checked roster, score and caps; false when no log runs or no
     * change seq is left.
     */
    bool create(const uint32_t uidA, const uint32_t uidB, const uint8_t scoreA, const uint8_t scoreB,
                const uint8_t flags, const uint8_t writer, GameRecord *created = nullptr) {
        if (!writable()) return false;
        GameRecord r;
        r.id = newGameId();
        r.order = nextOrder();
        r.uidA = uidA;
        r.uidB = uidB;
        r.scoreA = scoreA;
        r.scoreB = scoreB;
        r.flags = static_cast<uint8_t>(flags & GameRecord::KNOWN_FLAGS);
        insert(r, writer);
        if (created != nullptr) *created = r;
        return true;
    }

    // Edit (`deleted` false) or delete of the record at `sortIndex`: rev + 1, the rest as given.
    bool rewrite(const size_t sortIndex, GameRecord r, const uint8_t writer) {
        if (!writable() || sortIndex >= entries.size() || entries[sortIndex].record.deleted()) return false;
        const GameRecord &old = entries[sortIndex].record;
        r.id = old.id;
        r.order = old.order;
        r.rev = static_cast<uint8_t>(old.rev + 1);
        r.flags &= GameRecord::KNOWN_FLAGS;
        replace(sortIndex, r, writer);
        return true;
    }

    // RECORD write / pull. Roster and score checks of a live record are the caller's.
    Merge merge(const GameRecord &r, const uint8_t writer) {
        if (logEpoch == 0 || r.id == 0 || r.order == 0 || (r.flags & ~GameRecord::KNOWN_FLAGS) != 0) return Merge::Dropped;
        const int at = find(r.id);
        if (at < 0) {
            if (full() || (!r.deleted() && liveCount() >= liveCap())) return Merge::Dropped;
            insert(r, writer);
            return Merge::Stored;
        }
        const GameRecord &old = entries[static_cast<size_t>(at)].record;
        if (old.order != r.order) return Merge::Dropped;
        if (!r.beats(old)) return Merge::NoOp;
        if (full()) return Merge::Dropped;
        replace(static_cast<size_t>(at), r, writer);
        return Merge::Replaced;
    }

    // Pairing the slot again: its earlier writes are the board's from now on.
    void resetWriter(const uint8_t slot) {
        for (Entry &e: entries) {
            if (e.writer == slot) e.writer = BOARD;
        }
    }

    // --- Reads ---

    /**
     * CHANGES: the record with the smallest last change seq >= `from`, skipping `reader`'s own
     * writes. False = END. The caller's cursor moves to `seq` + 1.
     */
    bool changeAt(const uint16_t from, const uint8_t reader, GameRecord &out, uint16_t &seq) const {
        const Entry *best = nullptr;
        for (const Entry &e: entries) {
            if (e.seq < from || e.writer == reader) continue;
            if (best == nullptr || e.seq < best->seq) best = &e;
        }
        if (best == nullptr) return false;
        out = best->record;
        seq = best->seq;
        return true;
    }

    // count (1 B) + every live record's 18 B image in (order, id) order.
    uint32_t gamesCrc() const {
        const uint8_t count = static_cast<uint8_t>(liveCount());
        uint32_t crc = Garmin::crc32(&count, 1);
        uint8_t image[GameRecord::SIZE];
        for (const Entry &e: entries) {
            if (e.record.deleted()) continue;
            e.record.toBytes(image);
            crc = Garmin::crc32(image, GameRecord::SIZE, crc);
        }
        return crc;
    }

    // ACK_CHANGES' board-wide part; false when `upTo` is past changes_to.
    bool ack(const uint16_t upTo) {
        if (upTo > changesTo) return false;
        if (upTo > ackedTo) ackedTo = upTo;
        armed = true;
        return true;
    }

    bool unsynced() const {
        return armed && changesTo > ackedTo;
    }

    uint16_t unsyncedCount() const {
        return unsynced() ? static_cast<uint16_t>(changesTo - ackedTo) : 0;
    }

private:
    std::vector<Entry> entries;
    uint16_t changesTo = 0;
    uint16_t ackedTo = 0;
    bool armed = false;
    uint16_t lostTo = 0;
    uint8_t logEpoch = 0;
    uint16_t rev = 0;
    bool reserved = false;

    // The one gate of every write: nothing lands outside a running tournament (epoch 0).
    bool writable() const {
        return logEpoch != 0 && changesTo < SEQ_MAX;
    }

    // Unique among the stored records, so also unique among every id this log ever handed out
    // that a peer could still hold.
    uint32_t newGameId() const {
        uint32_t v = 0;
        while (v == 0 || find(v) >= 0) v = esp_random();
        return v;
    }

    uint16_t nextOrder() const {
        uint16_t top = 0;
        for (const Entry &e: entries) {
            if (e.record.order > top) top = e.record.order;
        }
        return top == SEQ_MAX ? top : static_cast<uint16_t>(top + 1);
    }

    size_t positionOf(const GameRecord &r) const {
        size_t i = 0;
        while (i < entries.size() && entries[i].record.sortsBefore(r)) i++;
        return i;
    }

    bool liveAfter(const size_t sortIndex) const {
        for (size_t i = sortIndex + 1; i < entries.size(); i++) {
            if (!entries[i].record.deleted()) return true;
        }
        return false;
    }

    void insert(const GameRecord &r, const uint8_t writer) {
        const size_t at = positionOf(r);
        entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(at), Entry{r, ++changesTo, writer});
        // A live record that sorts last is an append; anywhere else it renumbers turns.
        if (!r.deleted() && liveAfter(at)) rev++;
        if (r.deleted()) evictTombstone();
    }

    // Same id and order, so the sort position stays.
    void replace(const size_t at, const GameRecord &r, const uint8_t writer) {
        const bool wasLive = !entries[at].record.deleted();
        entries[at] = Entry{r, ++changesTo, writer};
        if (wasLive) rev++;
        if (wasLive && r.deleted()) evictTombstone();
    }

    void evictTombstone() {
        size_t tombstones = 0;
        size_t oldest = entries.size();
        for (size_t i = 0; i < entries.size(); i++) {
            if (!entries[i].record.deleted()) continue;
            tombstones++;
            if (oldest == entries.size() || entries[i].seq < entries[oldest].seq) oldest = i;
        }
        if (tombstones <= TOMBSTONE_MAX) return;
        if (entries[oldest].seq > lostTo) lostTo = entries[oldest].seq;
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(oldest));
    }
};

#endif //TOURNAMENT_LOG_H
