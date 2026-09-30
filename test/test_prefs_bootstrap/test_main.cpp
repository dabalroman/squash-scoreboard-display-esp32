// PrefsBootstrap::seed - the v1_bootstrap first-boot seed. Board-agnostic.
#include <unity.h>

#include <cstring>

#include "PrefsData.h"

void setUp() {}
void tearDown() {}

void test_seed_sets_credentials_and_dev_mode() {
    PrefsData d;
    PrefsBootstrap::seed(d, "HouseNet", "s3cret pw");
    TEST_ASSERT_EQUAL_STRING("HouseNet", d.wifiSSID);
    TEST_ASSERT_EQUAL_STRING("s3cret pw", d.wifiPassword);
    TEST_ASSERT_EQUAL_UINT8(1, d.enableDevMode);
}

void test_seed_resets_the_rest_to_defaults() {
    PrefsData d;
    d.brightness = 5;
    d.buzzerMode = 0;
    PrefsBootstrap::seed(d, "a", "");
    TEST_ASSERT_EQUAL_UINT8(PrefsData().brightness, d.brightness);
    TEST_ASSERT_EQUAL_UINT8(PrefsData().buzzerMode, d.buzzerMode);
    TEST_ASSERT_EQUAL_STRING("", d.wifiPassword);  // open network
}

void test_default_buzzer_is_in_match() {
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::IN_MATCH, PrefsData().buzzerMode);
    PrefsData d;
    PrefsBootstrap::seed(d, "a", "");
    TEST_ASSERT_EQUAL_UINT8(PrefsBuzzer::IN_MATCH, d.buzzerMode);
}

void test_seed_max_length_keeps_the_nul() {
    char ssid[64], pass[64];
    memset(ssid, 'S', 63); ssid[63] = '\0';
    memset(pass, 'P', 63); pass[63] = '\0';
    PrefsData d;
    PrefsBootstrap::seed(d, ssid, pass);
    TEST_ASSERT_EQUAL_STRING(ssid, d.wifiSSID);
    TEST_ASSERT_EQUAL_STRING(pass, d.wifiPassword);
    TEST_ASSERT_EQUAL_CHAR('\0', d.wifiSSID[63]);
    TEST_ASSERT_EQUAL_CHAR('\0', d.wifiPassword[63]);
}

void test_seed_truncates_overlong_input_terminated() {
    char big[100];
    memset(big, 'x', 99); big[99] = '\0';
    PrefsData d;
    PrefsBootstrap::seed(d, big, big);
    TEST_ASSERT_EQUAL_size_t(63, strlen(d.wifiSSID));
    TEST_ASSERT_EQUAL_size_t(63, strlen(d.wifiPassword));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_seed_sets_credentials_and_dev_mode);
    RUN_TEST(test_seed_resets_the_rest_to_defaults);
    RUN_TEST(test_default_buzzer_is_in_match);
    RUN_TEST(test_seed_max_length_keeps_the_nul);
    RUN_TEST(test_seed_truncates_overlong_input_terminated);
    return UNITY_END();
}
