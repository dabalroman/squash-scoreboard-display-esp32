// Assertion helpers shared by every suite.
#ifndef TEST_COMMON_CHECK_H
#define TEST_COMMON_CHECK_H

#include <cstdarg>
#include <cstdio>
#include <string>

#include <unity.h>

#include "FastLED.h"

inline std::string strf(const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

// TEST_ASSERT_MESSAGE, but the message is formatted only on failure: several
// suites assert inside loops of 10^5+ iterations.
#define CHECK(condition, ...)                                   \
    do {                                                        \
        if (!(condition)) TEST_FAIL_MESSAGE(strf(__VA_ARGS__).c_str()); \
    } while (0)

inline std::string rgb(const CRGB &c) {
    return strf("(%d,%d,%d)", c.r, c.g, c.b);
}

#define CHECK_RGB(expected, actual, ...)                                                        \
    do {                                                                                        \
        const CRGB checkExpected_ = (expected);                                                 \
        const CRGB checkActual_ = (actual);                                                     \
        if (!(checkExpected_ == checkActual_)) {                                                \
            TEST_FAIL_MESSAGE((strf(__VA_ARGS__) + ": got " + rgb(checkActual_) + ", expected " \
                               + rgb(checkExpected_)).c_str());                                 \
        }                                                                                       \
    } while (0)

inline void fillPixels(CRGB *pixels, const int count, const CRGB color) {
    for (int i = 0; i < count; i++) pixels[i] = color;
}

#endif
