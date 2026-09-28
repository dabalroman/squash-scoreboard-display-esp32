// Definitions the shim declares extern. Include exactly once per suite (in test_main.cpp).
#ifndef TEST_COMMON_HOST_GLOBALS_H
#define TEST_COMMON_HOST_GLOBALS_H

#include "FastLED.h"

uint32_t g_fakeMillis = 0;
CFastLED FastLED;

#endif
