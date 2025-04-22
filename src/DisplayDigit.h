#ifndef DISPLAYDIGIT_H
#define DISPLAYDIGIT_H

#include <Arduino.h>

/**
 * LEDs and segment ids (masks)
 *
 *                0x40
 *             71, 72, 73
 *          53            63
 *    0x10  52            62  0x20
 *          51            61
 *             41, 42, 43
 *          23    0x8     33
 *     0x2  22            32  0x4
 *          21            31
 *             11, 12, 13
 *                0x1
 */

constexpr uint8_t segmentToDigitsMap[10] = {
    0b01110111, // 0
    0b00100100, // 1
    0b01101011, // 2
    0b01101101, // 3
    0b00111100, // 4
    0b01011101, // 5
    0b01011111, // 6
    0b01100100, // 7
    0b01111111, // 8
    0b01111101, // 9
};

struct DisplayDigitSegment {
    uint8_t pixelIndexes[3];
};

struct DisplayDigitLeds {
    DisplayDigitSegment parts[7];
};

constexpr DisplayDigitLeds digitA = {
    {
        {85, 84, 83},
        {49, 48, 47},
        {50, 51, 52},
        {74, 75, 76},
        {46, 45, 44},
        {53, 54, 55},
        {73, 72, 71}
    },
};

constexpr DisplayDigitLeds digitB = {
    {
        {82, 81, 80},
        {61, 60, 59},
        {62, 63, 64},
        {77, 78, 79},
        {58, 57, 56},
        {65, 66, 67},
        {70, 69, 68}
    },
};


constexpr DisplayDigitLeds digitC = {
    {
        {43, 42, 41},
        {7, 6, 5},
        {8, 9, 10},
        {32, 33, 34},
        {4, 3, 2},
        {11, 12, 13},
        {31, 30, 29}
    },
};

constexpr DisplayDigitLeds digitD = {
    {
        {40, 39, 38},
        {19, 18, 17},
        {20, 21, 22},
        {35, 36, 37},
        {16, 15, 14},
        {23, 24, 25},
        {28, 27, 26}
    },
};

class DisplayDigit {
    DisplayDigitSegment parts[7] = {

    };
};


#endif //DISPLAYDIGIT_H
