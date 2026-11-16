#ifndef GAME_RECORD_H
#define GAME_RECORD_H

#include <stdint.h>
#include <string.h>

/**
 * One game of a tournament (docs/garmin-protocol.md 20.2): the 18-byte RECORD image every peer
 * stores, merges and forwards unchanged.
 */
struct GameRecord {
    enum : uint8_t { SIZE = 18, DELETED = 0x01, OFFLINE = 0x02, KNOWN_FLAGS = DELETED | OFFLINE };

    uint32_t id = 0;
    uint8_t rev = 0;
    uint16_t order = 0;
    uint32_t uidA = 0;
    uint32_t uidB = 0;
    uint8_t scoreA = 0;
    uint8_t scoreB = 0;
    uint8_t flags = 0;

    bool deleted() const {
        return (flags & DELETED) != 0;
    }

    bool offline() const {
        return (flags & OFFLINE) != 0;
    }

    void toBytes(uint8_t out[SIZE]) const {
        put32(out, id);
        out[4] = rev;
        out[5] = static_cast<uint8_t>(order);
        out[6] = static_cast<uint8_t>(order >> 8);
        put32(out + 7, uidA);
        put32(out + 11, uidB);
        out[15] = scoreA;
        out[16] = scoreB;
        out[17] = flags;
    }

    static GameRecord fromBytes(const uint8_t in[SIZE]) {
        GameRecord r;
        r.id = get32(in);
        r.rev = in[4];
        r.order = static_cast<uint16_t>(in[5] | (in[6] << 8));
        r.uidA = get32(in + 7);
        r.uidB = get32(in + 11);
        r.scoreA = in[15];
        r.scoreB = in[16];
        r.flags = in[17];
        return r;
    }

    // (order, id), both unsigned.
    bool sortsBefore(const GameRecord &o) const {
        return order != o.order ? order < o.order : id < o.id;
    }

    // The merge rule for two records with the same id: sticky delete, then serial rev, then the
    // larger byte image; an identical one never wins.
    bool beats(const GameRecord &stored) const {
        if (deleted() != stored.deleted()) return deleted();
        const uint8_t d = static_cast<uint8_t>(rev - stored.rev);
        if (d >= 1 && d <= 127) return true;
        if (d >= 129) return false;
        uint8_t mine[SIZE];
        uint8_t theirs[SIZE];
        toBytes(mine);
        stored.toBytes(theirs);
        return memcmp(mine, theirs, SIZE) > 0;
    }

private:
    static void put32(uint8_t *p, const uint32_t v) {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16);
        p[3] = static_cast<uint8_t>(v >> 24);
    }

    static uint32_t get32(const uint8_t *p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
               | (static_cast<uint32_t>(p[3]) << 24);
    }
};

#endif //GAME_RECORD_H
