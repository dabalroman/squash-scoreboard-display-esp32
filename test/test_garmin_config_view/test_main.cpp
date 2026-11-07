// GarminView's PARUJ row (#512): with GARMIN off it enables the mode and waits for BLE,
// and opens the pairing screen only once running(). GarminPairingView's outcome (#523):
// green ring then out of CONFIG, red ring then GARMIN, C/D at once. Both boards.
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

    bool windowOpen = false;
    uint32_t completed = 0;
    int closeCalls = 0;
    uint16_t openPairing() {
        windowOpen = true;
        return 1234;
    }
    void closePairing() {
        closeCalls++;
        windowOpen = false;
    }
    bool pairingOpen() const { return windowOpen; }
    uint32_t pairingRemainingMs() const { return windowOpen ? 60000 : 0; }
    uint32_t completedPairings() const { return completed; }
};

#include <vector>

#include <unity.h>

#include "BatterySensor.h"
#include "DeviceMode/ConfigMode/Views/GarminView.h"
#include "DeviceMode/ConfigMode/Views/GarminPairingView.h"
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

// ---------------------------------------------------------------- pairing outcome ---

struct PairingRig {
    GarminService service;
    RemoteInputManager remote{1, 2, 3, 4};
    CRGB pixels[Board::LED_COUNT];
    LedDisplay led{pixels};
    std::vector<ConfigModeState> states;
    int pairedCalls = 0;
    int leaveCalls = 0;
    GarminPairingView view;

    PairingRig()
        : view(service,
               [this](const ConfigModeState s) { states.push_back(s); },
               [this] { pairedCalls++; },
               [this] { leaveCalls++; }) {
        g_fakeMillis = 10000;
        view.initLedDisplay(led);
        tick();
    }

    // One ConfigMode::loop() pass, 50 ms later. The shim's FastLED.clear() has no buffer.
    void tick() {
        g_fakeMillis += 50;
        for (int i = 0; i < Board::LED_COUNT; i++) pixels[i] = CRGB(0, 0, 0);
        view.handleInput(remote);
        view.renderLedDisplay(led);
    }

    void press(RemoteInput &button) {
        g_fakeMillis += 1000;
        button.trigger();
        tick();
    }

    // Past the 1 s ring, then the pass that hands over.
    void runRingOut() {
        for (int i = 0; i < 25; i++) tick();
    }

    // Something is lit, and only in the ring's hue (Colors::Green is not pure).
    bool litOnlyIn(const bool green) const {
        int lit = 0;
        for (int i = 0; i < Board::LED_COUNT; i++) {
            const CRGB p = pixels[i];
            if (p.r == 0 && p.g == 0 && p.b == 0) continue;
            const uint8_t main = green ? p.g : p.r;
            const uint8_t other = green ? p.r : p.g;
            if (main <= other || main <= p.b) return false;
            lit++;
        }
        return lit > 0;
    }
};

static void test_paired_green_ring_then_leaves_config() {
    PairingRig r;
    TEST_ASSERT_FALSE(r.view.outcomeShown());
    r.service.completed = 1;
    r.tick();
    TEST_ASSERT_TRUE(r.view.outcomeShown());    // ConfigMode::goBack() refuses from here
    TEST_ASSERT_EQUAL(1, r.pairedCalls);       // the beep starts with the ring
    TEST_ASSERT_FALSE(r.service.windowOpen);    // closed at once, not on the way out
    TEST_ASSERT_TRUE(r.led.ringActive());
    r.tick();
    TEST_ASSERT_TRUE(r.litOnlyIn(true));
    TEST_ASSERT_EQUAL(0, r.leaveCalls);

    r.runRingOut();
    TEST_ASSERT_FALSE(r.led.ringActive());
    TEST_ASSERT_TRUE(r.view.outcomeShown());    // still refused in the hand-over tick
    TEST_ASSERT_EQUAL(1, r.leaveCalls);
    TEST_ASSERT_EQUAL(1, r.pairedCalls);
    TEST_ASSERT_TRUE(r.states.empty());         // not back to GARMIN: closed window is no expiry
}

static void test_expiry_red_ring_then_garmin() {
    PairingRig r;
    r.service.windowOpen = false;
    r.tick();
    TEST_ASSERT_TRUE(r.led.ringActive());
    r.tick();
    TEST_ASSERT_TRUE(r.litOnlyIn(false));
    TEST_ASSERT_TRUE(r.states.empty());

    r.runRingOut();
    TEST_ASSERT_EQUAL(1, r.states.size());
    TEST_ASSERT_TRUE(r.states[0] == ConfigModeState::Garmin);
    TEST_ASSERT_EQUAL(0, r.pairedCalls);
    TEST_ASSERT_EQUAL(0, r.leaveCalls);
}

static void test_cancel_leaves_without_ring() {
    PairingRig r;
    r.press(r.remote.buttonD);
    TEST_ASSERT_EQUAL(1, r.states.size());
    TEST_ASSERT_TRUE(r.states[0] == ConfigModeState::Garmin);
    TEST_ASSERT_FALSE(r.led.ringActive());
    TEST_ASSERT_EQUAL(0, r.pairedCalls);
}

static void test_presses_during_ring_are_ignored() {
    PairingRig r;
    r.service.completed = 1;
    r.tick();
    r.remote.buttonD.trigger();   // inside the ring: dropped, the view keeps its course
    r.tick();
    TEST_ASSERT_TRUE(r.states.empty());
    TEST_ASSERT_TRUE(r.led.ringActive());
    r.runRingOut();
    TEST_ASSERT_EQUAL(1, r.leaveCalls);
    TEST_ASSERT_TRUE(r.states.empty());
}

// restoreView() after an Overlay mid-ring: no replay, the view hands over.
static void test_reinit_mid_ring_hands_over() {
    PairingRig r;
    r.service.completed = 1;
    r.tick();
    r.view.initLedDisplay(r.led);
    TEST_ASSERT_FALSE(r.led.ringActive());
    r.tick();
    TEST_ASSERT_EQUAL(1, r.leaveCalls);
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
    RUN_TEST(test_paired_green_ring_then_leaves_config);
    RUN_TEST(test_expiry_red_ring_then_garmin);
    RUN_TEST(test_cancel_leaves_without_ring);
    RUN_TEST(test_presses_during_ring_are_ignored);
    RUN_TEST(test_reinit_mid_ring_hands_over);
    return UNITY_END();
}
