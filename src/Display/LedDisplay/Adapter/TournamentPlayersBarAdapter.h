#ifndef TOURNAMENT_PLAYERS_BAR_ADAPTER_H
#define TOURNAMENT_PLAYERS_BAR_ADAPTER_H

#include <vector>
#include <algorithm>
#include "../LedBar.h"
#include "UserProfile.h"

class TournamentPlayersBarAdapter {
    static bool isActive(const UserProfile *user, const std::vector<UserProfile *> &activePlayers) {
        return std::find(activePlayers.begin(), activePlayers.end(), user) != activePlayers.end();
    }

public:
    static std::array<LedBarPixel, LedBar::PIXEL_COUNT> toLedBarPixels(
        const std::vector<UserProfile *> &allUsers,
        const std::vector<UserProfile *> &activePlayers
    ) {
        std::array<LedBarPixel, LedBar::PIXEL_COUNT> pixels = {};

        uint8_t sections = 0;
        for (const UserProfile *user : allUsers) {
            if (isActive(user, activePlayers)) sections++;
        }

        if (sections == 0) return pixels;

        const uint8_t segment = (LedBar::PIXEL_COUNT - (sections - 1)) / sections;
        if (segment == 0) return pixels;

        uint8_t slot = 0;
        for (const UserProfile *user : allUsers) {
            if (!isActive(user, activePlayers)) continue;

            const Color &color = user->getColor();
            const CRGB crgb(color.r, color.g, color.b);
            const uint8_t start = slot * (segment + 1);

            for (uint8_t j = 0; j < segment; j++) {
                pixels[start + j].color = crgb;
            }

            slot++;
        }

        return pixels;
    }
};

#endif //TOURNAMENT_PLAYERS_BAR_ADAPTER_H
