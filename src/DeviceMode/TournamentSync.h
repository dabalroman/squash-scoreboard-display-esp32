#ifndef TOURNAMENT_SYNC_H
#define TOURNAMENT_SYNC_H

#include <stdint.h>
#include <string.h>

#include "RemoteDevelopmentService/LoggerHelper.h"
#include "DeviceMode/DeviceModeState.h"
#include "Garmin/GarminProtocol.h"
#include "Garmin/WatchCommand.h"
#include "Garmin/WatchSport.h"
#include "Tournament/Tournament.h"
#include "Tournament/TournamentRoster.h"

/**
 * The roster stage (spec 20.2): GarminService in main.cpp, a fake in the host tests. loop() only.
 */
class TournamentLink {
public:
    virtual ~TournamentLink() = default;
    // Entries 0..count-1 (clamped to the stage) into `out`, then the stage is cleared.
    virtual void takeRosterStage(uint8_t count, uint8_t *out) = 0;
};

// What the sport modes need for spec 20; null in a mode = no BEGIN growth, leave at once.
struct TournamentContext {
    TournamentLink &link;
    // Leaving with changes no peer has ACKed (20.6): main.cpp's warning, which leaves on a
    // second long C. Null leaves at once.
    void (*warnUnsynced)(uint16_t unsynced);
};

// A checked BEGIN on MENU, handed to the sport mode it builds.
struct TournamentJoin {
    uint32_t tournamentId;
    uint8_t sport;
    uint8_t count;
    uint32_t rosterCrc;
    uint8_t entries[Garmin::TOURNAMENT_ROSTER_MAX * Garmin::ROSTER_ENTRY_SIZE];
};

namespace TournamentSync {
    /**
     * BEGIN_TOURNAMENT on MENU (20.3). APPLIED fills `join` and names the sport's mode in `mode`;
     * the checks are Tournament::join()'s on a fresh tournament, so the build cannot refuse it.
     * Either answer clears the stage.
     */
    inline Garmin::AckStatus checkJoin(TournamentLink &link, const WatchCommand &command, TournamentJoin &join,
                                       DeviceModeState &mode) {
        memset(&join, 0, sizeof(join));
        link.takeRosterStage(command.count, join.entries);
        // modeForSport knows only the sports the menu lists.
        if (command.tournamentId == 0 || !WatchSport::modeForSport(command.sport, mode)
            || !TournamentRoster::valid(command.count, command.crc, join.entries)) {
            return Garmin::AckStatus::Invalid;
        }
        join.tournamentId = command.tournamentId;
        join.sport = command.sport;
        join.count = command.count;
        join.rosterCrc = command.crc;
        printLn("Tournament: joining %08x, sport %u, %u entries", static_cast<unsigned>(command.tournamentId),
                command.sport, command.count);
        return Garmin::AckStatus::Applied;
    }

    /**
     * A sport mode's BEGIN (roster growth), ACK_CHANGES and SET_SIDE. `onMatchScreen` =
     * MATCH_START..GAME_OVER; ACK_CHANGES is also taken on CHOOSE_PLAYERS.
     */
    inline Garmin::AckStatus handle(const TournamentContext *context, Tournament &tournament,
                                    const WatchCommand &command, const bool onMatchScreen) {
        switch (command.id) {
            case Garmin::CommandId::BeginTournament: {
                if (context == nullptr) return Garmin::AckStatus::WrongScreen;
                // Every answer clears the stage (20.2).
                uint8_t entries[Garmin::TOURNAMENT_ROSTER_MAX * Garmin::ROSTER_ENTRY_SIZE] = {};
                context->link.takeRosterStage(command.count, entries);
                if (!onMatchScreen) return Garmin::AckStatus::WrongScreen;
                return tournament.growRoster(command.tournamentId, command.sport, command.count, command.crc,
                                             entries);
            }
            case Garmin::CommandId::AckChanges:
                if (command.boundTid != command.tournamentId) return Garmin::AckStatus::Invalid;
                return tournament.ackChanges(command.tournamentId, command.boundEpoch, command.upTo);
            case Garmin::CommandId::SetSide:
                if (!onMatchScreen) return Garmin::AckStatus::WrongScreen;
                return tournament.reassignSide(command.side, command.uid);
            default:
                return Garmin::AckStatus::WrongScreen;
        }
    }

    // A person leaving the tournament (20.6): a warning first while it is unsynced.
    template <typename OnModeChange>
    void leave(const TournamentContext *context, const Tournament &tournament, OnModeChange onDeviceModeChange) {
        const uint16_t unsynced = tournament.unsyncedCount();
        if (unsynced > 0 && context != nullptr && context->warnUnsynced != nullptr) {
            context->warnUnsynced(unsynced);
            return;
        }
        onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
    }

    // A peer's BACK on CHOOSE_PLAYERS (20.6): refused while unsynced.
    template <typename OnModeChange>
    Garmin::AckStatus leaveFromWatch(const Tournament &tournament, OnModeChange onDeviceModeChange) {
        if (tournament.unsynced()) return Garmin::AckStatus::Invalid;
        onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
        return Garmin::AckStatus::Applied;
    }
}

#endif //TOURNAMENT_SYNC_H
