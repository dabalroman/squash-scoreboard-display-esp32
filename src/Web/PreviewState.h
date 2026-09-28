#ifndef PREVIEW_STATE_H
#define PREVIEW_STATE_H

#include <stdint.h>

#include "Color.h"

// One side of the LED preview pair. Constructors, not member initialisers: under
// gnu++11 those make it a non-aggregate and PreviewSlot{id, color, used} would not compile.
struct PreviewSlot {
    uint8_t id;
    Color color;
    bool used;

    PreviewSlot() : id(0), color(), used(false) {
    }

    PreviewSlot(const uint8_t id, const Color color, const bool used) : id(id), color(color), used(used) {
    }
};

/**
 * What POST /preview asked the LEDs to show - state only, never NVS, never a draw.
 * PlayerSetupView polls takeDirty() on its own cadence and renders from the slots.
 */
class PreviewState {
    // side 0 = left, 1 = right (matches the P<l>P<r> glyph layout). No per-id
    // memory beyond these two: every POST carries the colour to show.
    PreviewSlot slots[2];
    bool active = false;
    bool dirty = false;

public:
    // On open and close, so a pair from the previous PROFILE visit never survives
    // into the next. Dirty, so the view's next render falls back to the static word.
    void reset() {
        slots[0] = PreviewSlot{};
        slots[1] = PreviewSlot{};
        active = false;
        dirty = true;
    }

    /**
     * The edited player is always on the left. A different id slides the one shown
     * on the left over to the right (so an id already on the right swaps sides);
     * the first preview since reset() pairs it with `firstPartner`.
     */
    void show(const uint8_t playerId, const Color color, const PreviewSlot &firstPartner) {
        dirty = true;

        if (active && slots[0].used && slots[0].id == playerId) {
            slots[0].color = color;
            return;
        }

        if (!active) {
            slots[1] = firstPartner;
            active = true;
        } else {
            slots[1] = slots[0];
        }

        slots[0] = PreviewSlot{playerId, color, true};
    }

    // Returns and clears the flag - the view's one dirty-check per render, so an
    // update landing between renders is never missed nor rendered twice.
    bool takeDirty() {
        const bool wasDirty = dirty;
        dirty = false;
        return wasDirty;
    }

    bool hasPreview() const {
        return active;
    }

    const PreviewSlot &slot(const uint8_t side) const {
        return slots[side < 2 ? side : 0];
    }
};

#endif //PREVIEW_STATE_H
