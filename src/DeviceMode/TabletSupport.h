#ifndef TABLET_SUPPORT_H
#define TABLET_SUPPORT_H

#include <stdint.h>

#include "RemoteDevelopmentService/LoggerHelper.h"
#include "DeviceMode/DeviceModeState.h"
#include "Garmin/GarminProtocol.h"
#include "Garmin/WatchCommand.h"
#include "Garmin/WatchSport.h"
#include "Tournament/TabletSession.h"
#include "Tournament/Tournament.h"

/**
 * What tablet mode (spec 20) needs from the BLE side: GarminService in main.cpp, a fake in
 * the host tests. loop() only.
 */
class TabletLink {
public:
    virtual ~TabletLink() = default;
    // Entries 0..count-1 (clamped to the stage) into `out`, then the stage is cleared.
    virtual void takeRosterStage(uint8_t count, uint8_t *out) = 0;
    virtual void takeMirrorStage(uint8_t count, uint8_t *out) = 0;
    // The pushed roster's wire table; its CRC becomes the tablet slot's rosterVersion.
    virtual void setTabletRoster(const uint8_t *entries, uint8_t count) = 0;
};

// Handed to a sport mode built for a tablet tournament; null = board mode.
struct TabletContext {
    TabletSession &session;
    TabletLink &link;
};

namespace TabletSupport {
    /**
     * TOURNAMENT_WAIT's BEGIN_TOURNAMENT (spec 20.2). APPLIED fills `session`, publishes its
     * roster and names the sport's mode in `mode`; INVALID changes nothing. Either clears the stage.
     */
    inline Garmin::AckStatus begin(TabletSession &session, TabletLink &link, const WatchCommand &command,
                                   DeviceModeState &mode) {
        if (command.id != Garmin::CommandId::BeginTournament) return Garmin::AckStatus::WrongScreen;
        uint8_t entries[Garmin::TABLET_ROSTER_MAX * Garmin::ROSTER_ENTRY_SIZE] = {};
        link.takeRosterStage(command.count, entries);
        // modeForSport knows only the sports the menu lists.
        if (!WatchSport::modeForSport(command.sport, mode)
            || !session.begin(command.tournamentId, command.sport, command.count, command.crc, entries)) {
            return Garmin::AckStatus::Invalid;
        }
        link.setTabletRoster(session.wire(), session.count());
        printLn("Tablet: tournament %08x, sport %u, %u entries", static_cast<unsigned>(command.tournamentId),
                command.sport, session.count());
        return Garmin::AckStatus::Applied;
    }

    /**
     * A tablet sport mode on MATCH_START..GAME_OVER (`onTournamentScreen`): roster re-push,
     * mirror commit and outbox ACK. SET_SIDE is not wired yet (WRONG_SCREEN).
     */
    inline Garmin::AckStatus handle(TabletContext *tablet, Tournament &tournament, const WatchCommand &command,
                                    const bool onTournamentScreen) {
        if (tablet == nullptr || !onTournamentScreen) return Garmin::AckStatus::WrongScreen;
        TabletSession &session = tablet->session;

        switch (command.id) {
            case Garmin::CommandId::BeginTournament: {
                uint8_t entries[Garmin::TABLET_ROSTER_MAX * Garmin::ROSTER_ENTRY_SIZE] = {};
                tablet->link.takeRosterStage(command.count, entries);
                const uint8_t before = session.count();
                if (!session.replace(command.tournamentId, command.sport, command.count, command.crc, entries)) {
                    return Garmin::AckStatus::Invalid;
                }
                // Appended entries join selected; existing ones were updated in place.
                for (uint8_t i = before; i < session.count(); i++) tournament.addPlayer(*session.profiles()[i]);
                tablet->link.setTabletRoster(session.wire(), session.count());
                return Garmin::AckStatus::Applied;
            }
            case Garmin::CommandId::CommitMirror: {
                // 1.9 KB: static, off loop()'s stack; loop() is the only caller.
                static uint8_t games[Garmin::RESULTS_MAX * Garmin::GAME_ENTRY_SIZE];
                tablet->link.takeMirrorStage(command.count, games);
                if (command.count > Garmin::RESULTS_MAX) return Garmin::AckStatus::Invalid;
                return tournament.applyMirror(command.tournamentId, command.count, command.crc, games);
            }
            case Garmin::CommandId::AckOutbox:
                return tournament.ackOutbox(command.tournamentId, command.upTo);
            default:
                return Garmin::AckStatus::WrongScreen;
        }
    }
}

#endif //TABLET_SUPPORT_H
