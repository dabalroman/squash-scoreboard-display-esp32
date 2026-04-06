#ifndef GAME_SCORE_HISTORY_BAR_ADAPTER_H
#define GAME_SCORE_HISTORY_BAR_ADAPTER_H

#include <Color.h>
#include "../LedBar.h"
#include "../../../Tournament/Game/GameScoreHistory.h"

class GameScoreHistoryBarAdapter {
public:
    static std::array<LedBarPixel, LedBar::PIXEL_COUNT> toLedBarPixels(
        const Color &sideAColor,
        const Color &sideBColor,
        const GameScoreHistory &history
    ) {
        std::array<LedBarPixel, LedBar::PIXEL_COUNT> pixels = {};
        const auto &entries = history.getHistory();
        const uint8_t total = entries.size();
        const uint8_t count = std::min<uint8_t>(total, LedBar::PIXEL_COUNT);
        const uint8_t offset = total > LedBar::PIXEL_COUNT ? total - LedBar::PIXEL_COUNT : 0;

        for (uint8_t i = 0; i < count; i++) {
            const auto &entry = entries[offset + i];
            const Color &color = entry.side == GameSide::a ? sideAColor : sideBColor;

            pixels[i].color = CRGB(color.r, color.g, color.b);
            pixels[i].isBlinking = entry.status != GameScoreHistoryStatus::committed;
        }

        return pixels;
    }
};

#endif //GAME_SCORE_HISTORY_BAR_ADAPTER_H
