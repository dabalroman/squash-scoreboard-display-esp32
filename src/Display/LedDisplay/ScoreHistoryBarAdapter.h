#ifndef SCORE_HISTORY_BAR_ADAPTER_H
#define SCORE_HISTORY_BAR_ADAPTER_H

#include <Color.h>
#include "LedBar.h"
#include "Match/MatchScoreHistory.h"

class ScoreHistoryBarAdapter {
public:
    static std::array<LedBarPixel, BAR_DISPLAY_AMOUNT_OF_PIXELS> toLedBarPixels(
        const Color &sideAColor,
        const Color &sideBColor,
        const MatchScoreHistory &history
    ) {
        std::array<LedBarPixel, BAR_DISPLAY_AMOUNT_OF_PIXELS> pixels = {};
        const auto &entries = history.getHistory();
        const uint8_t total = entries.size();
        const uint8_t count = std::min<uint8_t>(total, BAR_DISPLAY_AMOUNT_OF_PIXELS);
        const uint8_t offset = total > BAR_DISPLAY_AMOUNT_OF_PIXELS ? total - BAR_DISPLAY_AMOUNT_OF_PIXELS : 0;

        for (uint8_t i = 0; i < count; i++) {
            const auto &entry = entries[offset + i];
            const Color &color = entry.side == MatchSide::a ? sideAColor : sideBColor;

            pixels[i].color = CRGB(color.r, color.g, color.b);
            pixels[i].isBlinking = entry.status != MatchScoreHistoryStatus::committed;
        }

        return pixels;
    }
};

#endif //SCORE_HISTORY_BAR_ADAPTER_H
