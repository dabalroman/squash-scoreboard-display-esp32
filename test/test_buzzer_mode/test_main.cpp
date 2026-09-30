// PrefsBuzzer: the stored buzzer byte and its CONFIG cycle. Board-agnostic.
#include <unity.h>

#include "PrefsData.h"

void setUp() {}
void tearDown() {}

void test_next_follows_display_order() {
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::IN_MATCH, PrefsBuzzer::next(PrefsBuzzer::OFF));
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::ALWAYS, PrefsBuzzer::next(PrefsBuzzer::IN_MATCH));
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::OFF, PrefsBuzzer::next(PrefsBuzzer::ALWAYS));
}

void test_prev_follows_display_order() {
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::ALWAYS, PrefsBuzzer::prev(PrefsBuzzer::OFF));
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::OFF, PrefsBuzzer::prev(PrefsBuzzer::IN_MATCH));
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::IN_MATCH, PrefsBuzzer::prev(PrefsBuzzer::ALWAYS));
}

void test_full_cycle_returns_to_start() {
    for (uint8_t start = 0; start < 3; start++) {
        uint8_t m = start;
        for (int i = 0; i < 3; i++) {
            m = PrefsBuzzer::next(m);
        }
        TEST_ASSERT_EQUAL_UINT8(start, m);
        for (int i = 0; i < 3; i++) {
            m = PrefsBuzzer::prev(m);
        }
        TEST_ASSERT_EQUAL_UINT8(start, m);
    }
}

// Old firmware reads any non-zero byte as "on"; the cycle enters at ALWAYS.
void test_unknown_byte_is_always() {
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::OFF, PrefsBuzzer::next(3));
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::IN_MATCH, PrefsBuzzer::prev(200));
}

// The CONFIG tickbox state and the /api/settings value.
void test_normalize_maps_unknown_to_always() {
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::OFF, PrefsBuzzer::normalize(PrefsBuzzer::OFF));
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::ALWAYS, PrefsBuzzer::normalize(PrefsBuzzer::ALWAYS));
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::IN_MATCH, PrefsBuzzer::normalize(PrefsBuzzer::IN_MATCH));
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::ALWAYS, PrefsBuzzer::normalize(3));
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::ALWAYS, PrefsBuzzer::normalize(255));
}

void test_stored_values_keep_their_meaning() {
    TEST_ASSERT_EQUAL_UINT8(0, PrefsBuzzer::OFF);
    TEST_ASSERT_EQUAL_UINT8(1, PrefsBuzzer::ALWAYS);
    TEST_ASSERT_EQUAL_UINT8(2, PrefsBuzzer::IN_MATCH);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_next_follows_display_order);
    RUN_TEST(test_prev_follows_display_order);
    RUN_TEST(test_full_cycle_returns_to_start);
    RUN_TEST(test_unknown_byte_is_always);
    RUN_TEST(test_normalize_maps_unknown_to_always);
    RUN_TEST(test_stored_values_keep_their_meaning);
    return UNITY_END();
}
