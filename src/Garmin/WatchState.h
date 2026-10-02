#ifndef GARMIN_WATCH_STATE_H
#define GARMIN_WATCH_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "GarminProtocol.h"

/**
 * The STATE body (spec 11.2/11.3) minus its per-connection part. Views describe their screen
 * through the typed setters; the service adds ackSeq/ackStatus/rosterVersion per connection
 * in build(). Never write body bytes by hand.
 */
class WatchState {
public:
    // Largest screen data: MENU with every sport (2 + n), padel PLAYING (15).
    // An enum, not a static constexpr member: C++11 has no inline variables to back an odr-use.
    enum : uint8_t { MAX_DATA = 24 };

    struct Playing {
        uint32_t leftUid;
        uint32_t rightUid;
        bool uncommitted;
        bool tiebreak;
        bool gameBallLeft;
        bool gameBallRight;
        uint8_t leftGames;
        uint8_t rightGames;
        uint8_t leftScore;
        uint8_t rightScore;
    };

    struct Result {
        uint32_t leftUid;
        uint32_t rightUid;
        uint8_t winnerSide;
        uint8_t leftScore;
        uint8_t rightScore;
        uint8_t leftGames;
        uint8_t rightGames;
        bool bajgiel;
    };

    WatchState() {
        setBusy(Garmin::ScreenId::Booting);
    }

    void setSport(const Garmin::SportId value) {
        sport = value;
    }

    // BOOTING, CONFIG, PROFILE, or any screen the watch shows as "board busy".
    void setBusy(const Garmin::ScreenId value) {
        begin(value);
    }

    // `count` is clamped to what fits.
    void setMenu(const Garmin::SportId cursor, const Garmin::SportId *sports, uint8_t count) {
        begin(Garmin::ScreenId::Menu);
        if (count > MAX_DATA - 2) count = MAX_DATA - 2;
        put8(static_cast<uint8_t>(cursor));
        put8(count);
        for (uint8_t i = 0; i < count; i++) put8(static_cast<uint8_t>(sports[i]));
    }

    void setChoosePlayers(const uint32_t selectedMask) {
        begin(Garmin::ScreenId::ChoosePlayers);
        put32(selectedMask);
    }

    void setMatchStart(const uint32_t selectedMask, const uint32_t leftUid, const uint32_t rightUid) {
        begin(Garmin::ScreenId::MatchStart);
        put32(selectedMask);
        put32(leftUid);
        put32(rightUid);
    }

    void setIntro(const uint32_t leftUid, const uint32_t rightUid) {
        begin(Garmin::ScreenId::Intro);
        put32(leftUid);
        put32(rightUid);
    }

    // Squash and both volleyballs.
    void setPlaying(const Playing &p) {
        beginPlaying(p);
    }

    // Padel: games = sets, score = gems, points = PadelPoint (or raw tiebreak points).
    void setPadelPlaying(const Playing &p, const uint8_t leftPoint, const uint8_t rightPoint) {
        beginPlaying(p);
        put8(leftPoint);
        put8(rightPoint);
    }

    void setCelebration(const Result &r) {
        putResult(Garmin::ScreenId::Celebration, r);
    }

    void setGameOver(const Result &r) {
        putResult(Garmin::ScreenId::GameOver, r);
    }

    Garmin::ScreenId getScreen() const {
        return screen;
    }

    Garmin::SportId getSport() const {
        return sport;
    }

    uint8_t bodySize() const {
        return static_cast<uint8_t>(Garmin::STATE_HEADER + dataLen);
    }

    // Writes bodySize() bytes; `out` must hold Garmin::STATE_HEADER + MAX_DATA.
    uint8_t build(const uint8_t ackSeq, const Garmin::AckStatus ackStatus, const uint32_t rosterVersion,
                  uint8_t *out) const {
        out[0] = static_cast<uint8_t>(screen);
        out[1] = static_cast<uint8_t>(sport);
        out[2] = ackSeq;
        out[3] = static_cast<uint8_t>(ackStatus);
        Garmin::putU32(out + 4, rosterVersion);
        memcpy(out + Garmin::STATE_HEADER, data, dataLen);
        return bodySize();
    }

    // Change detection for publishing; the ack part is per connection and compared there.
    bool operator==(const WatchState &other) const {
        return screen == other.screen && sport == other.sport && dataLen == other.dataLen
               && memcmp(data, other.data, dataLen) == 0;
    }

    bool operator!=(const WatchState &other) const {
        return !(*this == other);
    }

private:
    Garmin::ScreenId screen = Garmin::ScreenId::Booting;
    Garmin::SportId sport = Garmin::SportId::None;
    uint8_t data[MAX_DATA] = {};
    uint8_t dataLen = 0;

    void begin(const Garmin::ScreenId value) {
        screen = value;
        dataLen = 0;
    }

    void put8(const uint8_t v) {
        if (dataLen < MAX_DATA) data[dataLen++] = v;
    }

    void put32(const uint32_t v) {
        for (uint8_t i = 0; i < 4; i++) put8(static_cast<uint8_t>(v >> (8 * i)));
    }

    void beginPlaying(const Playing &p) {
        begin(Garmin::ScreenId::Playing);
        put32(p.leftUid);
        put32(p.rightUid);
        put8(static_cast<uint8_t>((p.uncommitted ? 0x01 : 0) | (p.tiebreak ? 0x02 : 0)
                                  | (p.gameBallLeft ? 0x04 : 0) | (p.gameBallRight ? 0x08 : 0)));
        put8(p.leftGames);
        put8(p.rightGames);
        put8(p.leftScore);
        put8(p.rightScore);
    }

    void putResult(const Garmin::ScreenId value, const Result &r) {
        begin(value);
        put32(r.leftUid);
        put32(r.rightUid);
        put8(r.winnerSide);
        put8(r.leftScore);
        put8(r.rightScore);
        put8(r.leftGames);
        put8(r.rightGames);
        put8(r.bajgiel ? 1 : 0);
    }
};

#endif //GARMIN_WATCH_STATE_H
