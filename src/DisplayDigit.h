#ifndef DISPLAYDIGIT_H
#define DISPLAYDIGIT_H

/**
 *      71, 72, 73
 *   53            63
 *   52            62
 *   51            61
 *      41, 42, 43
 *   23            33
 *   22            32
 *   21            31
 *      11, 12, 13
 */

#include <Arduino.h>

struct DisplayDigitPart {
    uint8_t pixelIndexes[3];
};

struct DisplayDigitLeds {
    DisplayDigitPart parts[7];
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
        {40,39,38},
        {19, 18, 17},
        {20,21,22},
        {35,36,37},
        {16,15,14},
        {23,24,25},
        {28,27,26}
    },
};

class DisplayDigit {
    DisplayDigitPart parts[7] = {

    };
};


#endif //DISPLAYDIGIT_H
