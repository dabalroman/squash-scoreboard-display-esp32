#ifndef GARMIN_WATCH_COMMAND_H
#define GARMIN_WATCH_COMMAND_H

#include <stddef.h>
#include <stdint.h>

#include "GarminProtocol.h"

/**
 * A parsed COMMAND write (spec 10). Views get only well-formed commands with a known id;
 * argument checks that need the screen (side, uid, sport) are theirs.
 */
struct WatchCommand {
    Garmin::CommandId id;
    uint8_t seq;
    uint8_t sport;      // SELECT_SPORT
    uint8_t side;       // SCORE, UNDO
    uint32_t uid;       // TOGGLE_PLAYER
    uint32_t leftUid;   // SET_PAIR
    uint32_t rightUid;  // SET_PAIR

    bool hasValidSide() const {
        return side <= 1;
    }
};

namespace Garmin {
    enum class CommandParse : uint8_t {
        Ok,
        Drop,       // foreign PROTO, or no seq to acknowledge: no ack at all
        Malformed,  // ack MALFORMED with `seq`
        UnknownCmd, // ack UNKNOWN_CMD with `seq`
    };

    // Full frame size per id, 0 = unknown.
    inline uint8_t commandSize(const uint8_t id) {
        switch (static_cast<CommandId>(id)) {
            case CommandId::SelectSport:
            case CommandId::Score:
            case CommandId::Undo:
                return 4;
            case CommandId::TogglePlayer:
                return 7;
            case CommandId::SetPair:
                return 11;
            case CommandId::Back:
            case CommandId::StartTournament:
            case CommandId::SwapSides:
            case CommandId::StartMatch:
            case CommandId::Skip:
            case CommandId::NextGame:
            case CommandId::Sync:
                return 3;
        }
        return 0;
    }

    // Shorter than the id's layout -> Malformed; longer is fine (spec 2: trailing bytes are
    // ignored, room for compatible additions). `out.seq` is set whenever the result is acked.
    inline CommandParse parseCommand(const uint8_t *in, const size_t len, WatchCommand &out) {
        out = WatchCommand{};
        if (len < 3 || in[0] != PROTO) return CommandParse::Drop;
        out.seq = in[2];
        const uint8_t size = commandSize(in[1]);
        if (size == 0) return CommandParse::UnknownCmd;
        if (len < size) return CommandParse::Malformed;

        out.id = static_cast<CommandId>(in[1]);
        switch (out.id) {
            case CommandId::SelectSport:
                out.sport = in[3];
                break;
            case CommandId::Score:
            case CommandId::Undo:
                out.side = in[3];
                break;
            case CommandId::TogglePlayer:
                out.uid = getU32(in + 3);
                break;
            case CommandId::SetPair:
                out.leftUid = getU32(in + 3);
                out.rightUid = getU32(in + 7);
                break;
            default:
                break;
        }
        return CommandParse::Ok;
    }

    inline AckStatus ackFor(const CommandParse parse) {
        return parse == CommandParse::Malformed ? AckStatus::Malformed
               : parse == CommandParse::UnknownCmd ? AckStatus::UnknownCmd
               : AckStatus::Applied;
    }
}

#endif //GARMIN_WATCH_COMMAND_H
