#ifndef BATTERY_GAUGE_H
#define BATTERY_GAUGE_H

#include <stdint.h>

// Bars lit in the battery icon's three-segment gauge. Shared by the e-paper and the OLED so
// both icons read the same at any charge; coarse on purpose - the percent beside it is exact.
inline uint8_t batteryGaugeBars(const int16_t percent) {
    return percent >= 80 ? 3 : (percent >= 50 ? 2 : (percent >= 20 ? 1 : 0));
}

#endif //BATTERY_GAUGE_H
