#ifndef WATCH_SUPPORT_H
#define WATCH_SUPPORT_H

#include <vector>

#include "UserProfile.h"
#include "DeviceMode/Celebration/CelebrationVariant.h"
#include "Garmin/WatchCommand.h"
#include "Garmin/GarminProtocol.h"
#include "Garmin/WatchState.h"
#include "Tournament/Match/Match.h"
#include "Tournament/Game/GameResult.h"
#include "Tournament/Game/GameScoreHistory.h"

/**
 * What the sport views share for the watch (spec 10, 11.3). Host-includable: no
 * View, no display, so the routing and the screen data are tested without the views.
 */
namespace WatchSupport {
    // BEGIN_TOURNAMENT, ACK_CHANGES, SET_SIDE (spec 20): the mode's, never the view's.
    inline bool isTournamentCommand(const Garmin::CommandId id) {
        return id == Garmin::CommandId::BeginTournament || id == Garmin::CommandId::AckChanges
               || id == Garmin::CommandId::SetSide;
    }

    /**
     * Mode-level routing. A setState() lands at the mode's next loop(), so while one is
     * pending the view on screen is the outgoing one and must not take the command.
     */
    template <typename GoBack, typename EditGames, typename ToTournament, typename ToView>
    Garmin::AckStatus route(const WatchCommand &command, const bool viewChangePending, GoBack goBack,
                            EditGames editGames, ToTournament toTournament, ToView toView) {
        if (viewChangePending) {
            return Garmin::AckStatus::Busy;
        }
        if (command.id == Garmin::CommandId::Back) {
            return goBack();
        }
        if (command.id == Garmin::CommandId::AddOfflineGame || command.id == Garmin::CommandId::RemoveOfflineGame) {
            return editGames();
        }
        if (isTournamentCommand(command.id)) {
            return toTournament();
        }
        return toView();
    }

    // ADD / REMOVE_OFFLINE_GAME (spec 10.2 0x0F / 0x10); null = not on a tournament screen. The
    // sender's pairing slot is the change's writer. A template so this header needs no
    // Tournament.h (its printLn is the includer's).
    template <typename TournamentT>
    Garmin::AckStatus editOfflineGames(TournamentT *tournament, const WatchCommand &command) {
        if (tournament == nullptr) {
            return Garmin::AckStatus::WrongScreen;
        }
        if (command.id == Garmin::CommandId::AddOfflineGame) {
            return tournament->addOfflineGame(command.uidA, command.uidB, command.scoreA, command.scoreB,
                                              command.writer);
        }
        return tournament->removeOfflineGame(command.tournamentId, command.rev, command.index, command.writer);
    }

    inline GameSide sideOf(const WatchCommand &command) {
        return command.side == 0 ? GameSide::a : GameSide::b;
    }

    // Bit i = index i of the active roster `roster` (spec 11.3) for which `isIn` holds.
    template <typename IsIn>
    uint32_t selectedMask(const std::vector<UserProfile *> &roster, IsIn isIn) {
        uint32_t mask = 0;
        for (size_t i = 0; i < roster.size() && i < 32; i++) {
            if (isIn(*roster[i])) {
                mask |= 1UL << i;
            }
        }
        return mask;
    }

    // MATCH_START: the tournament roster is active and every entry is in.
    inline uint32_t selectedMask(const std::vector<UserProfile *> &roster) {
        return selectedMask(roster, [](const UserProfile &) { return true; });
    }

    // -1 when no profile carries `uid`.
    inline int indexOfUid(const std::vector<UserProfile *> &players, const uint32_t uid) {
        for (size_t i = 0; i < players.size(); i++) {
            if (players[i]->getUid() == uid) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Uids and the match standing; the caller fills in the game.
    inline WatchState::Playing playing(const Match &match, const UserProfile &left, const UserProfile &right) {
        WatchState::Playing p = {};
        p.leftUid = left.getUid();
        p.rightUid = right.getUid();
        const MatchResult standing = match.getMatchResult();
        p.leftGames = standing.scoreOf(left.getId());
        p.rightGames = standing.scoreOf(right.getId());
        p.sidesSwapped = match.sidesSwapped();
        return p;
    }

    // LAST_KNOWN / LAST_RIGHT (spec 11.3): the watch's BACK-key undo after a reconnect.
    // GameSide::a is the left court side in every PLAYING view.
    inline bool lastPoint(WatchState::Playing &p, const GameScoreHistory &history) {
        GameSide side = GameSide::none;
        if (!history.newest(side)) {
            return false;
        }
        p.lastKnown = true;
        p.lastRight = side == GameSide::b;
        return true;
    }

    // CELEBRATION and GAME_OVER. `match` already holds `result`, so the games include it.
    inline WatchState::Result result(const Match &match, const GameResult &result, const UserProfile &left,
                                     const UserProfile &right, const uint8_t leftScore, const uint8_t rightScore) {
        WatchState::Result r = {};
        r.leftUid = left.getUid();
        r.rightUid = right.getUid();
        r.winnerSide = result.winnerPlayerId == left.getId() ? 0 : 1;
        r.leftScore = leftScore;
        r.rightScore = rightScore;
        const MatchResult standing = match.getMatchResult();
        r.leftGames = standing.scoreOf(left.getId());
        r.rightGames = standing.scoreOf(right.getId());
        r.bajgiel = selectCelebrationVariant(match, result) == CelebrationVariant::Bajgiel;
        r.sidesSwapped = match.sidesSwapped();
        return r;
    }

    // A RESULTS entry (spec 19) for listed game `index` (< listedCount()): the record's own A/B,
    // not court sides. A template for the same reason as editOfflineGames().
    template <typename TournamentT>
    void resultEntry(const TournamentT &tournament, const size_t index, uint8_t out[Garmin::RESULT_ENTRY_SIZE]) {
        const GameRecord &record = tournament.listedRecord(index);
        uint8_t wireTurn = tournament.turnOf(index);
        if (record.offline()) {
            wireTurn |= Garmin::TURN_OFFLINE;
        }
        Garmin::buildResultEntry(record.uidA, record.uidB, record.scoreA, record.scoreB, wireTurn, out);
    }
}

#endif //WATCH_SUPPORT_H
