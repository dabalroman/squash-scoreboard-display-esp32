#ifndef LED_BLEND_H
#define LED_BLEND_H

#include <stdint.h>
#include <FastLED.h>

/**
 * Per-pixel blend of an animation layer onto the base, 8-bit integer only (host and
 * device goldens stay bit-identical; it runs per pixel). Black is the transparent layer colour for every mode except
 * Multiply, where it is white - identityFor() gives the scratch fill.
 *
 *   Normal    premultiplied over, alpha = brightest channel of the layer
 *   Screen    255 - (255-a)(255-b)/255
 *   Add       a + b, clamped
 *   Lighten   per-channel max
 *   Multiply  a*b/255 - dims, never brightens
 */
enum class BlendMode : uint8_t { Normal, Screen, Add, Lighten, Multiply };

namespace LedBlend {
    /** x / 255 rounded down, exact for 0..255*255 - checked exhaustively on the host. */
    inline uint8_t div255(const uint16_t x) {
        return static_cast<uint8_t>((x + 1u + (x >> 8)) >> 8);
    }

    inline uint8_t channel(const uint8_t a, const uint8_t b, const BlendMode mode, const uint8_t alpha) {
        switch (mode) {
            case BlendMode::Screen:
                return static_cast<uint8_t>(255 - div255(static_cast<uint16_t>((255 - a) * (255 - b))));
            case BlendMode::Add: {
                const uint16_t sum = static_cast<uint16_t>(a + b);
                return sum > 255 ? 255 : static_cast<uint8_t>(sum);
            }
            case BlendMode::Lighten:
                return a > b ? a : b;
            case BlendMode::Multiply:
                return div255(static_cast<uint16_t>(a * b));
            case BlendMode::Normal:
            default: {
                const uint16_t out = static_cast<uint16_t>(div255(static_cast<uint16_t>(a * (255 - alpha))) + b);
                return out > 255 ? 255 : static_cast<uint8_t>(out);
            }
        }
    }
}

inline CRGB blendPixel(const CRGB base, const CRGB layer, const BlendMode mode) {
    uint8_t alpha = layer.r > layer.g ? layer.r : layer.g;
    if (layer.b > alpha) alpha = layer.b;

    return CRGB(
        LedBlend::channel(base.r, layer.r, mode, alpha),
        LedBlend::channel(base.g, layer.g, mode, alpha),
        LedBlend::channel(base.b, layer.b, mode, alpha)
    );
}

inline CRGB identityFor(const BlendMode mode) {
    return mode == BlendMode::Multiply ? CRGB(255, 255, 255) : CRGB(0, 0, 0);
}

inline const char *blendName(const BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal: return "NORMAL";
        case BlendMode::Screen: return "SCREEN";
        case BlendMode::Add: return "ADD";
        case BlendMode::Lighten: return "LIGHTEN";
        case BlendMode::Multiply: return "MULTIPLY";
    }
    return "?";
}

#endif //LED_BLEND_H
