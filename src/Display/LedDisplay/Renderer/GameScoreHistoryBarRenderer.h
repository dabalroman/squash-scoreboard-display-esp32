#ifndef GAME_SCORE_HISTORY_BAR_ADAPTER_H
#define GAME_SCORE_HISTORY_BAR_ADAPTER_H

#include <Color.h>
#include "../LedBar.h"
#include "../../../Tournament/Game/GameScoreHistory.h"

class GameScoreHistoryBarRenderer {
public:
    /**
     * Renders the game's score history onto the LED bar, one entry per point in
     * chronological order, in each side's color, blinking while uncommitted.
     *
     * @param maxPointWidth Widest a single point may render (in pixels). Points
     *        stay this wide while the whole history fits, then shrink toward 1px
     *        as it grows, so sports with few points (padel gems) still fill the
     *        bar. Defaults to 1 — the classic one-pixel-per-point look.
     * @param paddingWidth  Gap (in black pixels) inserted between points, like
     *        MatchResultBarRenderer. Applied at any point width, but dropped once
     *        the history no longer leaves room for it (e.g. paddingWidth 1 with
     *        more than 12 single-pixel points). Defaults to 0 (no gaps).
     */
    static std::array<LedBarPixel, LedBar::PIXEL_COUNT> toLedBarPixels(
        const Color &sideAColor,
        const Color &sideBColor,
        const GameScoreHistory &history,
        const uint8_t paddingWidth = 0,
        const uint8_t maxPointWidth = 1
    ) {
        std::array<LedBarPixel, LedBar::PIXEL_COUNT> pixels = {};
        const auto &entries = history.getHistory();
        const uint8_t total = entries.size();
        if (total == 0) return pixels;

        // Widest point width (down to 1px) at which the whole history still fits.
        uint8_t pointWidth = maxPointWidth < 1 ? 1 : maxPointWidth;
        while (pointWidth > 1
               && total * pointWidth + (total - 1) * paddingWidth > LedBar::PIXEL_COUNT) {
            pointWidth--;
        }

        // Padding stays at any point width while the whole history fits with it,
        // and is dropped only when there is no room left (e.g. paddingWidth 1
        // with more than 12 single-pixel points needs 25+ pixels).
        const bool paddingFits =
            total * pointWidth + (total - 1) * paddingWidth <= LedBar::PIXEL_COUNT;
        const uint8_t padding = paddingFits ? paddingWidth : 0;

        // Point + trailing gap. Fit as many most-recent points as the bar holds.
        const uint8_t block = pointWidth + padding;
        const uint8_t capacity = (LedBar::PIXEL_COUNT + padding) / block;
        const uint8_t count = std::min<uint8_t>(total, capacity);
        const uint8_t offset = total - count;

        for (uint8_t i = 0; i < count; i++) {
            const auto &entry = entries[offset + i];
            const Color &color = entry.side == GameSide::a ? sideAColor : sideBColor;
            const CRGB crgb(color.r, color.g, color.b);
            const bool blinking = entry.status != GameScoreHistoryStatus::committed;
            const uint8_t start = i * block;

            for (uint8_t j = 0; j < pointWidth && (start + j) < LedBar::PIXEL_COUNT; j++) {
                pixels[start + j].color = crgb;
                pixels[start + j].isBlinking = blinking;
            }
        }

        return pixels;
    }
};

#endif //GAME_SCORE_HISTORY_BAR_ADAPTER_H
