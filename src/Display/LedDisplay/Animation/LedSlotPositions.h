#ifndef LED_SLOT_POSITIONS_H
#define LED_SLOT_POSITIONS_H

#include <stdint.h>

#include "Board.h"

/**
 * V2: where every LED on the chain physically sits, in raw map units with the
 * origin at the e-paper centre, +x right, +y down (~16 units per mm, so the 197
 * between adjacent dies is ~12 mm).
 *
 * Generated from assets/led-map.svg by helpers/led_positions.py; `--check`
 * verifies this table still matches the map. Do not hand-edit the rows.
 *
 * SKIP marks a slot with no front-facing die: the two back indicators (4, 9) and
 * slot 2 of each digit module (12, 28, 44, 60), an unfitted chain position.
 * Animations must leave those untouched rather than writing black to them.
 *
 * Namespace scope, not a class member: a static constexpr array member is an ODR
 * link error on GCC 8.4.
 */

#if BOARD_REV == 2

namespace LedSlots {
    enum : int16_t { SKIP = -32768 };

    constexpr int16_t POS[Board::LED_COUNT][2] = {
            {  -342,   295},  //  0  border left,  bottom outer
            {  -342,    98},  //  1  border left,  bottom inner
            {  -342,   -98},  //  2  border left,  top inner
            {  -342,  -295},  //  3  border left,  top outer
            {  SKIP,     0},  //  4  back indicator B
            {   331,   295},  //  5  border right, bottom outer
            {   331,    98},  //  6  border right, bottom inner
            {   331,   -98},  //  7  border right, top inner
            {   331,  -295},  //  8  border right, top outer
            {  SKIP,     0},  //  9  back indicator A
            {  2203,   377},  // 10  digit D
            {  2203,   180},  // 11
            {  SKIP,     0},  // 12  dead slot 2
            {  2203,  -214},  // 13
            {  2203,  -411},  // 14
            {  2020,  -606},  // 15
            {  1823,  -606},  // 16
            {  1626,  -606},  // 17
            {  1662,  -214},  // 18
            {  1662,   -17},  // 19
            {  1662,   180},  // 20
            {  1626,   573},  // 21
            {  1823,   573},  // 22
            {  2020,   573},  // 23
            {  2097,   -16},  // 24
            {  1900,   -16},  // 25
            {  1245,   377},  // 26  digit C
            {  1245,   180},  // 27
            {  SKIP,     0},  // 28  dead slot 2
            {  1245,  -214},  // 29
            {  1245,  -411},  // 30
            {  1062,  -606},  // 31
            {   865,  -606},  // 32
            {   668,  -606},  // 33
            {   705,  -214},  // 34
            {   705,   -17},  // 35
            {   705,   180},  // 36
            {   668,   573},  // 37
            {   865,   573},  // 38
            {  1062,   573},  // 39
            {  1139,   -16},  // 40
            {   942,   -16},  // 41
            {  -717,   376},  // 42  digit B
            {  -717,   179},  // 43
            {  SKIP,     0},  // 44  dead slot 2
            {  -717,  -215},  // 45
            {  -717,  -412},  // 46
            {  -900,  -607},  // 47
            { -1097,  -607},  // 48
            { -1294,  -607},  // 49
            { -1258,  -215},  // 50
            { -1258,   -18},  // 51
            { -1258,   179},  // 52
            { -1294,   572},  // 53
            { -1097,   572},  // 54
            {  -900,   572},  // 55
            {  -823,   -17},  // 56
            { -1020,   -17},  // 57
            { -1675,   376},  // 58  digit A
            { -1675,   179},  // 59
            {  SKIP,     0},  // 60  dead slot 2
            { -1675,  -215},  // 61
            { -1675,  -412},  // 62
            { -1858,  -607},  // 63
            { -2055,  -607},  // 64
            { -2252,  -607},  // 65
            { -2215,  -215},  // 66
            { -2215,   -18},  // 67
            { -2215,   179},  // 68
            { -2252,   572},  // 69
            { -2055,   572},  // 70
            { -1858,   572},  // 71
            { -1781,   -17},  // 72
            { -1978,   -17},  // 73
    };
}

#endif

#endif //LED_SLOT_POSITIONS_H
