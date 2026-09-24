#ifndef LED_LAYER_STACK_H
#define LED_LAYER_STACK_H

#include <stdint.h>
#include <FastLED.h>

#include "Board.h"
#include "LedAnimation.h"
#include "LedBlend.h"
#include "LedTarget.h"

/**
 * Base (pixels[] as the glyphs/border/bar rendered it) plus two animation
 * layers, composed in place before showCompensated(). CPU-only on pixels[]: it
 * never touches FastLED's driver, so it cannot affect the RMT timing.
 */
class LedLayerStack {
public:
    enum Slot : uint8_t { LAYER_1 = 0, LAYER_2 = 1, LAYER_COUNT = 2 };

private:
    struct Layer {
        const LedAnimation *anim;
        BlendMode mode;
        uint16_t targets;
        LayerMask mask;
    };

    const uint16_t *elementMap;   ///< slot -> LedTarget bits, owned by LedDisplay
    Layer layers[LAYER_COUNT];
    CRGB scratch[Board::LED_COUNT];
    uint8_t lit[(Board::LED_COUNT + 7) / 8];

    bool isLit(const uint16_t slot) const {
        return lit[slot >> 3] >> (slot & 7) & 1;
    }

public:
    explicit LedLayerStack(const uint16_t *elementMap) : elementMap(elementMap) {
        clearAll();
    }

    void set(const Slot slot, const LedAnimation *anim, const BlendMode mode, const uint16_t targets, const LayerMask mask) {
        layers[slot] = Layer{anim, mode, targets, mask};
    }

    void clear(const Slot slot) {
        layers[slot] = Layer{nullptr, BlendMode::Normal, 0, LayerMask::AllSlots};
    }

    void clearAll() {
        clear(LAYER_1);
        clear(LAYER_2);
    }

    void compose(CRGB *pixels, const uint32_t nowMs) {
        bool litCaptured = false;

        for (uint8_t l = 0; l < LAYER_COUNT; l++) {
            const Layer &layer = layers[l];
            if (layer.anim == nullptr || !layer.anim->active(nowMs)) {
                continue;
            }

            // Captured once, before layer 1 lands: LitOnly means lit by the base.
            if (!litCaptured) {
                for (uint16_t i = 0; i < Board::LED_COUNT; i++) {
                    if (i % 8 == 0) lit[i >> 3] = 0;
                    if (pixels[i].r | pixels[i].g | pixels[i].b) lit[i >> 3] |= static_cast<uint8_t>(1u << (i & 7));
                }
                litCaptured = true;
            }

            const CRGB identity = identityFor(layer.mode);
            for (uint16_t i = 0; i < Board::LED_COUNT; i++) scratch[i] = identity;

            layer.anim->render(nowMs, scratch);

            for (uint16_t i = 0; i < Board::LED_COUNT; i++) {
                if (!(elementMap[i] & layer.targets)) continue;
                if (layer.mask == LayerMask::LitOnly && !isLit(i)) continue;
                pixels[i] = blendPixel(pixels[i], scratch[i], layer.mode);
            }
        }
    }
};

#endif //LED_LAYER_STACK_H
