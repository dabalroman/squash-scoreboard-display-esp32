#ifndef MATCH_ORDER_KEEPER_H
#define MATCH_ORDER_KEEPER_H
#include <vector>
#include <map>

#include "UserProfile.h"

struct MatchPlayersPair {
    uint8_t playerAId;
    uint8_t playerBId;
};

class PlayerKeeper {
public:
    uint8_t id;
    std::vector<uint8_t> playedWith;
    uint8_t waitingFor = 0;

    explicit PlayerKeeper(const uint8_t id = 0) : id(id), playedWith(std::vector<uint8_t>()) {
    }
};

class MatchOrderKeeper {
    std::map<uint8_t, PlayerKeeper> players;

    // void cleanupPlayedWithIfEveryoneAlreadyPlayedWithEachOther() {
    //     if (players.size() < 2) {
    //         return;
    //     }
    //
    //     const size_t amountOfPlayers = players.size();
    //
    //     bool shouldCleanup = true;
    //     for (auto it = players.begin(); it != players.end(); ++it) {
    //         if (it->second->playedWith.size() < amountOfPlayers - 1) {
    //             shouldCleanup = false;
    //             break;
    //         }
    //     }
    //
    //     if (!shouldCleanup) {
    //         return;
    //     }
    //
    //     printLn("Cleaning up playedWith");
    //
    //     for (const auto &player: players) {
    //         player.second->playedWith.clear();
    //     }
    // }

public:
    void setPlayers(const std::vector<UserProfile *> &_players) {
        players.clear();

        for (const auto *player: _players) {
            auto id = player->getId();
            players.emplace(id, PlayerKeeper(id));
            printLn("Added player %02d", id);
        }

        printLn("Amount of players is %d", players.size());
    }

    void addPlayer(const UserProfile *player) {
        if (players.count(player->getId())) {
            return;
        }

        players.emplace(player->getId(), PlayerKeeper(player->getId()));
    }

    // MatchPlayersPair getPlayersForNextMatch() const {
    //     if (players.size() < 2) {
    //         return MatchPlayersPair{0, 0};
    //     }
    //
    //     std::vector<PlayerKeeper *> candidateList;
    //     candidateList.reserve(players.size());
    //     for (auto &player: players) {
    //         candidateList.push_back(player.second);
    //     }
    //
    //     // Sort by the fewest matches, then by the longest wait time
    //     std::stable_sort(
    //         candidateList.begin(), candidateList.end(),
    //         [](const PlayerKeeper *a, const PlayerKeeper *b) {
    //             if (a->playedWith.size() != b->playedWith.size()) {
    //                 return a->playedWith.size() < b->playedWith.size();
    //             }
    //
    //             return a->waitingFor > b->waitingFor;
    //         }
    //     );
    //
    //     PlayerKeeper *p1 = candidateList[0];
    //     PlayerKeeper *p2 = candidateList[1];
    //
    //     for (size_t i = 1; i < candidateList.size(); i++) {
    //         auto *candidate = candidateList[i];
    //
    //         const bool hasAlreadyPlayedWith =
    //                 std::find(
    //                     p1->playedWith.begin(),
    //                     p1->playedWith.end(),
    //                     candidate->id
    //                 ) != p1->playedWith.end();
    //
    //         if (!hasAlreadyPlayedWith) {
    //             p2 = candidate;
    //             break;
    //         }
    //     }
    //
    //     if (p1->id > p2->id) {
    //         std::swap(p1, p2);
    //     }
    //
    //     printLn("Suggestion %d vs %d", p1->id, p2->id);
    //
    //     // return their IDs
    //     return MatchPlayersPair{p1->id, p2->id};
    // }

    void confirmMatchBetweenPlayers(const MatchPlayersPair pair) {
        const auto a = players.find(pair.playerAId);
        const auto b = players.find(pair.playerBId);

        if (a == players.end() || b == players.end()) {
            return;
        }

        printLn("Confirmed %d vs %d", pair.playerAId, pair.playerBId);

        a->second.playedWith.push_back(pair.playerBId);
        b->second.playedWith.push_back(pair.playerAId);

        for (auto &player: players) {
            player.second.waitingFor++;
        }

        a->second.waitingFor = 0;
        b->second.waitingFor = 0;

        // cleanupPlayedWithIfEveryoneAlreadyPlayedWithEachOther();
    }
};

#endif //MATCH_ORDER_KEEPER_H
