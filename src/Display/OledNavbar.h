#ifndef OLED_NAVBAR_H
#define OLED_NAVBAR_H

#include <stdint.h>

#include "BackDisplay.h"
#include "EInk/EInkDisplay.h"

/**
 * The OLED's top strip (y 1..7, built-in 5x7 font): a left note, the battery percent on
 * the right and the watch badge beside it. Values are passed in, so a view says what
 * it shows and the strip draws it.
 *
 * It is lit only on V1; V2's OLED has dead rows there and carries the same facts in the
 * e-paper footer. The gate is on that fact (no e-paper), never on the board, and lives
 * here alone so no view repeats it.
 */
struct OledNavbar {
    const char *left;       // nullptr = none
    int16_t batteryPercent; // < 0 = none
    bool watchBadge;

    explicit OledNavbar(const char *left = nullptr, const int16_t batteryPercent = -1,
                        const bool watchBadge = false)
        : left(left), batteryPercent(batteryPercent), watchBadge(watchBadge) {}

    void render(BackDisplay &backDisplay) const {
        if (EInkDisplay::available()) {
            return;
        }

        if (left != nullptr) {
            backDisplay.drawTopLeftNote(left);
        }
        if (batteryPercent >= 0) {
            backDisplay.drawBatteryPercent(static_cast<uint8_t>(batteryPercent));
        }
        if (watchBadge) {
            backDisplay.drawWatchBadge();
        }
    }
};

#endif //OLED_NAVBAR_H
