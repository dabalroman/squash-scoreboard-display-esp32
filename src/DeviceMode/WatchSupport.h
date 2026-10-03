#ifndef WATCH_SUPPORT_H
#define WATCH_SUPPORT_H

#include <vector>

#include "UserProfile.h"
#include "DeviceMode/Celebration/CelebrationVariant.h"
#include "Garmin/WatchCommand.h"
#include "Garmin/WatchState.h"
#include "Tournament/Match/Match.h"
#include "Tournament/Game/GameResult.h"

/**
 * What the sport views share for the watch (spec 10, 11.3). Host-includable: no
 * View, no display, so the routing and the screen data are tested without the views.
 */
namespace WatchSupport {
    /**
     * Mode-level routing. A setState() lands at the mode's next loop(), so while one is
     * pending the view on screen is the outgoing one and must not take the command.
     */
    template <typename GoBack, typename ToView>
    Garmin::AckStatus route(const WatchCommand &command, const bool viewChangePending, GoBack goBack,
                            ToView toView) {
        if (viewChangePending) {
            return Garmin::AckStatus::Busy;
        }
        if (command.id == Garmin::CommandId::Back) {
            return goBack() ? Garmin::AckStatus::Applied : Garmin::AckStatus::WrongScreen;
        }
        return toView();
    }

    inline GameSide sideOf(const WatchCommand &command) {
        return command.side == 0 ? GameSide::a : GameSide::b;
    }

    // Bit i = roster index i, which is what UserProfile::id is.
    inline uint32_t selectedMask(const std::vector<UserProfile *> &players) {
        uint32_t mask = 0;
        for (const UserProfile *player : players) {
            if (player->getId() < 32) {
                mask |= 1UL << player->getId();
            }
        }
        return mask;
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
        return p;
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
        return r;
    }
}

#endif //WATCH_SUPPORT_H
