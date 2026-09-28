#ifndef BAR_DISPLAY_H
#define BAR_DISPLAY_H

#include <FastLED.h>

#include "Tournament/GameSide.h"

struct LedBarPixel {
    CRGB color = CRGB::Black;
    bool isBlinking = false;
    GameSide side = GameSide::none;   // who scored it; none outside a game history
};

// V1 only. The celebration is LedSweepAnimation on a layer over the whole front
// (LedDisplay's LedLayerStack) - this class only ever shows the caller-supplied state.
class LedBar {
    constexpr static uint16_t BLINK_INTERVAL_MS = 500;
    constexpr static uint8_t FIRST_PIXEL_INDEX = 88;

public:
    constexpr static uint8_t PIXEL_COUNT = 24;

private:
    CRGB *pixels;
    std::array<LedBarPixel, PIXEL_COUNT> state;

public:
    explicit LedBar(CRGB *pixels) : pixels(pixels) {
    }

    void setState(std::array<LedBarPixel, PIXEL_COUNT> newState) {
        state = std::move(newState);
    }

    void markSlots(uint16_t *map, const uint16_t bit) const {
        for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
            map[FIRST_PIXEL_INDEX + i] |= bit;
        }
    }

    // Clears both bits first, so no owner survives a new state.
    void markOwners(uint16_t *map, const uint16_t leftBit, const uint16_t rightBit) const {
        for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
            uint16_t &slot = map[FIRST_PIXEL_INDEX + i];
            slot &= static_cast<uint16_t>(~(leftBit | rightBit));
            if (state[i].side == GameSide::a) slot |= leftBit;
            else if (state[i].side == GameSide::b) slot |= rightBit;
        }
    }

    void render(const uint32_t &tickMs) const {
        for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
            if (state[i].isBlinking && tickMs % BLINK_INTERVAL_MS < BLINK_INTERVAL_MS / 2) {
                continue;
            }

            pixels[FIRST_PIXEL_INDEX + i] = state[i].color;
        }
    }
};


#endif //BAR_DISPLAY_H
