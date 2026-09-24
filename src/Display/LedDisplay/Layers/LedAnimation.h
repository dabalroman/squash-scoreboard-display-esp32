#ifndef LED_ANIMATION_H
#define LED_ANIMATION_H

#include <stdint.h>
#include <FastLED.h>

/**
 * One animation layer's content. `out` is a full LED_COUNT buffer pre-filled
 * with the layer's identity colour (identityFor), so an animation writes only
 * the slots it lights or dims; the stack masks and blends it onto the base.
 */
class LedAnimation {
public:
    virtual ~LedAnimation() {}

    virtual bool active(uint32_t nowMs) const = 0;

    virtual void render(uint32_t nowMs, CRGB *out) const = 0;
};

#endif //LED_ANIMATION_H
