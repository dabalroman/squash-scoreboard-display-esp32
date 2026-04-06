#ifndef MATCH_RESULT_BAR_ADAPTER
#define MATCH_RESULT_BAR_ADAPTER

#include <Color.h>
#include "../LedBar.h"
#include "Tournament/Match/MatchResult.h"

class MatchResultBarAdapter {
    static constexpr uint8_t SIDE_PIXELS = LedBar::PIXEL_COUNT / 2;

    static std::array<LedBarPixel, SIDE_PIXELS> getPixelsForSide(const Color fillColor, const uint8_t score,
                                                                 const uint8_t maxScore) {
        const auto color = CRGB(fillColor.r, fillColor.g, fillColor.b);
        std::array<LedBarPixel, SIDE_PIXELS> pixels = {};

        uint8_t barWidth = 3;
        bool gaps = true;

        if (maxScore >= 4) {
            barWidth = 2;
        }

        if (maxScore >= 5) {
            barWidth = 1;
        }

        if (maxScore >= 7) {
            gaps = false;
        }

        for (uint8_t i = 0; i < SIDE_PIXELS; i++) {
            const uint8_t step = barWidth + (gaps ? 1 : 0);
            const bool isGap = gaps && i % step == 0;
            const bool isPoint = i < score * step;

            pixels[SIDE_PIXELS - 1 - i].color = !isPoint || isGap ? CRGB::Black : color;
        }

        return pixels;
    }

public:
    static std::array<LedBarPixel, LedBar::PIXEL_COUNT> toLedBarPixels(
        const Color &sideAColor,
        const Color &sideBColor,
        const MatchResult &matchResult,
        const bool arePlayersSwapped = false
    ) {
        std::array<LedBarPixel, LedBar::PIXEL_COUNT> pixels = {};

        const uint8_t maxScore = std::max(matchResult.playerAScore, matchResult.playerBScore);

        if (maxScore == 0) {
            return pixels;
        }

        const std::array<LedBarPixel, SIDE_PIXELS> left = getPixelsForSide(
            !arePlayersSwapped ? sideAColor : sideBColor, matchResult.playerAScore, maxScore
        );
        const std::array<LedBarPixel, SIDE_PIXELS> right = getPixelsForSide(
            arePlayersSwapped ? sideAColor : sideBColor, matchResult.playerBScore, maxScore
        );

        for (uint8_t i = 0; i < LedBar::PIXEL_COUNT; i++) {
            if (!arePlayersSwapped) {
                pixels[i] = i < SIDE_PIXELS
                                ? left[i]
                                : right[LedBar::PIXEL_COUNT - 1 - i];
            } else {
                pixels[i] = i < SIDE_PIXELS
                                ? right[i]
                                : left[LedBar::PIXEL_COUNT - 1 - i];
            }
        }

        return pixels;
    }
};

#endif //MATCH_RESULT_BAR_ADAPTER
