// GarminView's PARUJ row (#512): with GARMIN off it enables the mode and waits for BLE,
// and opens the pairing screen only once running(). Both boards.
#include "../test_garmin_views/view_stubs.h"   // first: pre-defines the display header guards

// Stand-in for GarminService (the real one needs NimBLE); the view only uses these calls.
#define GARMIN_SERVICE_H
class GarminService {
public:
    bool enabled = false;
    bool up = false;
    bool broken = false;
    uint8_t paired = 1;
    int setEnabledCalls = 0;

    bool isEnabled() const { return enabled; }
    void setEnabled(const bool on) {
        setEnabledCalls++;
        enabled = on;
    }
    bool running() const { return up; }
    bool failed() const { return broken; }
    uint8_t pairedCount() const { return paired; }
    uint8_t authedCount() const { return 0; }
    void forgetAll() { paired = 0; }
};

#include <vector>

#include <unity.h>

#include "BatterySensor.h"
#include "DeviceMode/ConfigMode/Views/GarminView.h"
#include "../common/host_globals.h"

void setUp() {}
void tearDown() {}

struct Rig {
    GarminService service;
    BatterySensor sensor;
    BatteryMonitor battery{sensor};
    RemoteInputManager remote{1, 2, 3, 4};
    std::vector<ConfigModeState> states;
    GarminView view;

    Rig() : view(service, battery, [this](const ConfigModeState s) { states.push_back(s); }) {}

    // Past the 500 ms re-trigger lock, then one press and one input pass.
    void press(RemoteInput &button) {
        g_fakeMillis += 1000;
        button.trigger();
        view.handleInput(remote);
    }

    void pass() {
        g_fakeMillis += 50;
        view.handleInput(remote);
    }

    void selectPair() { press(remote.buttonB); }
};

static void test_pair_while_off_enables_and_waits() {
    Rig r;
    r.selectPair();
    r.press(r.remote.buttonD);
    TEST_ASSERT_TRUE(r.service.enabled);
    TEST_ASSERT_EQUAL(1, r.service.setEnabledCalls);
    TEST_ASSERT_TRUE(r.states.empty());

    r.pass();
    TEST_ASSERT_TRUE(r.states.empty());   // still starting: no timeout, no early window

    r.service.up = true;
    r.pass();
    TEST_ASSERT_EQUAL(1, r.states.size());
    TEST_ASSERT_TRUE(r.states[0] == ConfigModeState::GarminPairing);

    r.pass();
    TEST_ASSERT_EQUAL(1, r.states.size());   // pending cleared: no second entry
}

static void test_pair_while_running_enters_at_once() {
    Rig r;
    r.service.enabled = r.service.up = true;
    r.selectPair();
    r.press(r.remote.buttonD);
    TEST_ASSERT_EQUAL(1, r.states.size());
    TEST_ASSERT_TRUE(r.states[0] == ConfigModeState::GarminPairing);
    TEST_ASSERT_EQUAL(0, r.service.setEnabledCalls);
}

static void test_a_b_cancel_the_wait() {
    // C defers to release (poll-driven), so it is not reachable through trigger().
    for (int k = 0; k < 2; k++) {
        Rig r;
        r.selectPair();
        r.press(r.remote.buttonD);
        r.press(k == 0 ? r.remote.buttonA : r.remote.buttonB);
        r.service.up = true;
        r.pass();
        TEST_ASSERT_TRUE(r.states.empty());
    }
}

static void test_d_on_another_row_cancels() {
    Rig r;
    r.selectPair();
    r.press(r.remote.buttonD);
    r.press(r.remote.buttonB);   // to ZAPOMNIJ
    r.press(r.remote.buttonD);   // arms forget
    r.service.up = true;
    r.pass();
    TEST_ASSERT_TRUE(r.states.empty());
}

static void test_failure_cancels() {
    Rig r;
    r.selectPair();
    r.press(r.remote.buttonD);
    r.service.broken = true;
    r.pass();
    r.service.broken = false;
    r.service.up = true;
    r.pass();
    TEST_ASSERT_TRUE(r.states.empty());
}

static void test_repeated_d_while_pending_keeps_waiting() {
    Rig r;
    r.selectPair();
    r.press(r.remote.buttonD);
    r.press(r.remote.buttonD);
    r.service.up = true;
    r.pass();
    TEST_ASSERT_EQUAL(1, r.states.size());
}

static void test_forget_keeps_enabled() {
    Rig r;
    r.service.enabled = r.service.up = true;
    r.selectPair();
    r.press(r.remote.buttonB);
    r.press(r.remote.buttonD);   // arm
    r.press(r.remote.buttonD);   // forget
    TEST_ASSERT_EQUAL(0, r.service.paired);
    TEST_ASSERT_TRUE(r.service.enabled);
    TEST_ASSERT_EQUAL(0, r.service.setEnabledCalls);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_pair_while_off_enables_and_waits);
    RUN_TEST(test_pair_while_running_enters_at_once);
    RUN_TEST(test_a_b_cancel_the_wait);
    RUN_TEST(test_d_on_another_row_cancels);
    RUN_TEST(test_failure_cancels);
    RUN_TEST(test_repeated_d_while_pending_keeps_waiting);
    RUN_TEST(test_forget_keeps_enabled);
    return UNITY_END();
}
