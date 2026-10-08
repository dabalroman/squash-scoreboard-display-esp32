// Host shim: a deterministic esp_random(), so seeded uids are reproducible.
#ifndef HOST_SHIM_ESP_RANDOM_H
#define HOST_SHIM_ESP_RANDOM_H

#include <cstdint>

inline uint32_t &hostRandomState() {
    static uint32_t s = 0x12345678u;
    return s;
}

inline uint32_t esp_random() {
    uint32_t &s = hostRandomState();
    s = s * 1664525u + 1013904223u;
    return s;
}

#endif
