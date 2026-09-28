// Per-board facts the shared suites check against, written out by hand so a
// change in src/ cannot move the expectation with it.
#ifndef TEST_COMMON_BOARD_CONFIG_H
#define TEST_COMMON_BOARD_CONFIG_H

#include <cstdint>

struct SlotRange {
    int lo, hi;

    bool contains(const int slot) const { return slot >= lo && slot <= hi; }
};

struct BoardConfig {
    const char *name;
    int ledCount;
    int indicatorA;   // slot of IndicatorPlayerA
    int indicatorB;

    // Boot-sweep reach probes: the ring gets from the origin out to both far corners.
    SlotRange sweepNear;
    SlotRange sweepFarA;
    SlotRange sweepFarB;

    // Element slot counts (0 = the board has none).
    int colonSlots;
    int barSlots;
    int borderTopSlots;
    int borderBottomSlots;

    int seamSlots;             // live slots at x == 0: V1's two colon dies, none on V2
    uint32_t introTotalMs;     // wipe + hold (+ out, only where something is outgoing)
};

#if BOARD_REV == 1

// V1 has no dead chain positions - only the two back indicators are reserved.
inline bool isReservedSlot(const int slot) {
    return slot == 2 || slot == 3;
}

static const BoardConfig BOARD{
    "v1", 112, 3, 2,
    {0, 1},     // near: colon
    {46, 87},   // far A: digits A/B
    {4, 45},    // far B: digits C/D
    2, 24, 0, 0,
    2,
    1600,       // wipe 1000 + hold 200 + out 400 (the bar is outgoing)
};

#elif BOARD_REV == 2

// Back indicators 4/9 and the dead chain position 2 of each digit module.
inline bool isReservedSlot(const int slot) {
    return slot == 4 || slot == 9 || slot == 12 || slot == 28 || slot == 44 || slot == 60;
}

static const BoardConfig BOARD{
    "v2", 74, 9, 4,
    {0, 9},     // near: border
    {58, 73},   // far A: digit A
    {10, 25},   // far B: digit D
    0, 0, 4, 4,
    0,
    1200,       // wipe 1000 + hold 200, no out phase - V2 has no bar
};

#endif

#endif
