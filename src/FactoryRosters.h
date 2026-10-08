#ifndef FACTORY_ROSTERS_H
#define FACTORY_ROSTERS_H

#include <stdint.h>

#include "PlayerPalette.h"

/**
 * The two rosters' NVS keys (namespace "ns") and their factory seeds. Squash takes
 * players; padel and volleyball take teams - never mixed. Each roster is its own
 * PlayersData blob with its own uids, so editing one never touches the other.
 */

// No uid here - those are generated when the list is seeded.
struct FactoryPlayer {
    const char *name;
    Color color;
};

namespace FactoryRosters {
    constexpr const char *PLAYERS_KEY = "ply";
    constexpr const char *TEAMS_KEY = "tms";

    // Namespace-scope const arrays have internal linkage, so including this twice is safe.
    const FactoryPlayer PLAYERS[] = {
        {"Adrian", PlayerColors::Green},
        {"Roman", PlayerColors::Yellow},
        {"Basia", PlayerColors::Magenta},
        {"Krystian", PlayerColors::Blue},
        {"Jola", PlayerColors::Red},
        {"Cegiel", PlayerColors::Pink}
    };
    constexpr uint8_t PLAYER_COUNT = sizeof(PLAYERS) / sizeof(PLAYERS[0]);

    // "My" / "Goscie" first: padel is usually played against strangers.
    const FactoryPlayer TEAMS[] = {
        {"My", PlayerColors::Blue},
        {"Goscie", PlayerColors::Orange},
        {"Czerwony", PlayerColors::Red},
        {"Zielony", PlayerColors::Green},
        {"Zolty", PlayerColors::Yellow},
        {"Fioletowy", PlayerColors::Purple},
        {"Rozowy", PlayerColors::Pink},
        {"Niebieski", PlayerColors::NavyBlue}
    };
    constexpr uint8_t TEAM_COUNT = sizeof(TEAMS) / sizeof(TEAMS[0]);
}

#endif //FACTORY_ROSTERS_H
