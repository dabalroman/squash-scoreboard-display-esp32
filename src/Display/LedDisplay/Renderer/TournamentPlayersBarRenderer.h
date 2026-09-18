#ifndef TOURNAMENT_PLAYERS_BAR_ADAPTER_H
#define TOURNAMENT_PLAYERS_BAR_ADAPTER_H

#include <vector>
#include <algorithm>
#include "../LedBar.h"
#include "UserProfile.h"

class TournamentPlayersBarRenderer {
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

        // Clamp rather than blank. With a 1 px gap between sections the segment
        // width hits 0 from 13 players up - which a 32-player roster makes easy to
        // reach - and the whole bar went dark. Past that point the gap is dropped
        // instead, so every section still gets its one pixel.
        uint8_t segment = (LedBar::PIXEL_COUNT - (sections - 1)) / sections;
        uint8_t stride = segment + 1;
        if (segment == 0) {
            segment = 1;
            stride = 1;
        }

        uint8_t slot = 0;
        for (const UserProfile *user : allUsers) {
            if (!isActive(user, activePlayers)) continue;

            const uint8_t start = slot * stride;
            // More selected players than pixels: the rest simply have no room.
            if (start >= LedBar::PIXEL_COUNT) break;

            const Color &color = user->getColor();
            const CRGB crgb(color.r, color.g, color.b);

            for (uint8_t j = 0; j < segment && start + j < LedBar::PIXEL_COUNT; j++) {
                pixels[start + j].color = crgb;
            }

            slot++;
        }

        return pixels;
    }
};

#endif //TOURNAMENT_PLAYERS_BAR_ADAPTER_H
