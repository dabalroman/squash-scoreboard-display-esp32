// Host shim for the Arduino core: only what the LED display layer needs.
#ifndef LED_DUMP_SHIM_ARDUINO_H
#define LED_DUMP_SHIM_ARDUINO_H

#include <array>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <algorithm>

typedef unsigned long ulong;

// Fake clock: set g_fakeMillis before calling anything that reads millis().
extern uint32_t g_fakeMillis;
inline uint32_t millis() { return g_fakeMillis; }

// No-op: the clock is fake, so host checks step renderFrame() rather than run play().
inline void delay(uint32_t) {}

#endif
