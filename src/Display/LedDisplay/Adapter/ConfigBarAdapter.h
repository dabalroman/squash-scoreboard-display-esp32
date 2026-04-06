#ifndef CONFIG_BAR_ADAPTER_H
#define CONFIG_BAR_ADAPTER_H

#include <Color.h>
#include "../LedBar.h"

class ConfigBarAdapter {
public:
    static std::array<LedBarPixel, LedBar::PIXEL_COUNT> toLedBarPixels(
        const uint8_t selectedOption,
        const bool enableBuzzer,
        const bool enableWifi
    ) {
        std::array<LedBarPixel, LedBar::PIXEL_COUNT> pixels = {};

        constexpr uint8_t OPTION_COUNT = 6;
        constexpr uint8_t SEGMENT = (LedBar::PIXEL_COUNT - (OPTION_COUNT - 1)) / OPTION_COUNT; // 3px

        const Color colors[OPTION_COUNT] = {
            Colors::White,                               // Brightness
            enableBuzzer ? Colors::Green : Colors::Red,  // Buzzer
            enableWifi   ? Colors::Green : Colors::Red,  // WiFi
            Colors::White,                               // IP address
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
