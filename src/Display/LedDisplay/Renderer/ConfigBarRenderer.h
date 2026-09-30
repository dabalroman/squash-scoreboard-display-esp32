#ifndef CONFIG_BAR_ADAPTER_H
#define CONFIG_BAR_ADAPTER_H

#include <Color.h>
#include "PrefsData.h"
#include "../LedBar.h"

class ConfigBarRenderer {
public:
    // Off red, in match yellow, always green; an unknown byte reads as always.
    static Color buzzerColor(const uint8_t buzzerMode) {
        return buzzerMode == PrefsBuzzer::OFF
                   ? Colors::Red
                   : buzzerMode == PrefsBuzzer::IN_MATCH ? Colors::Yellow : Colors::Green;
    }

    static std::array<LedBarPixel, LedBar::PIXEL_COUNT> toLedBarPixels(
        const uint8_t selectedOption,
        const uint8_t buzzerMode,
        const bool enableDevMode
    ) {
        std::array<LedBarPixel, LedBar::PIXEL_COUNT> pixels = {};

        constexpr uint8_t OPTION_COUNT = 5;
        constexpr uint8_t SEGMENT = (LedBar::PIXEL_COUNT - (OPTION_COUNT - 1)) / OPTION_COUNT; // 4px

        const Color colors[OPTION_COUNT] = {
            Colors::White,                               // Brightness
            buzzerColor(buzzerMode),                    // Buzzer
            enableDevMode ? Colors::Green : Colors::Red, // Dev Mode
            Colors::Pink,                                // Reboot
            Colors::Aqua,                                // Return
        };

        for (uint8_t i = 0; i < OPTION_COUNT; i++) {
            const Color &color = colors[i];
            const CRGB crgb(color.r, color.g, color.b);
            const uint8_t start = i * (SEGMENT + 1);

            for (uint8_t j = 0; j < SEGMENT; j++) {
                pixels[start + j].color = crgb;
                pixels[start + j].isBlinking = (i == selectedOption);
            }
        }

        return pixels;
    }
};

#endif //CONFIG_BAR_ADAPTER_H
