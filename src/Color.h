#ifndef COLOR_H
#define COLOR_H

#include <Arduino.h>

struct Color {
    union {
        struct {
            uint8_t r;
            uint8_t g;
            uint8_t b;
        };

        uint8_t raw[3];
    };

    constexpr Color() : r(0), g(0), b(0) {
    }

    constexpr Color(const uint8_t r, const uint8_t g, const uint8_t b) : r(r), g(g), b(b) {
    }

    explicit constexpr Color(const uint32_t hex)
        : r((hex >> 16) & 0xFF),
          g((hex >> 8) & 0xFF),
          b(hex & 0xFF) {
    }
};

namespace Colors {
    static constexpr auto White = Color(0xFFFFFF);
    static constexpr auto Green = Color(0x00FF00);
    static constexpr auto Red = Color(0xFF0000);
    static constexpr auto Black = Color(0x000000);
    static constexpr auto Blue = Color(0x0000FF);
    static constexpr auto Yellow = Color(0xFFFF00);
    static constexpr auto Magenta = Color(0xFF00FF);
    static constexpr auto Cyan = Color(0x00FFFF);
}

#endif //COLOR_H
