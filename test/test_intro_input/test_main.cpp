// Fob presses during the match intro (#524): the press ends the intro and acts once on
// GamePlaying, with one buzzer tick; the timer hand-over and an overlay still drop latches.
// Drives the real RemoteInputManager through the real sport modes. Both boards.
#define VIEW_STUBS_OWN_DIGITAL_READ
#include <cstdint>
static uint8_t g_pinHigh[8] = {};
inline int digitalRead(const uint8_t pin) { return pin < 8 && g_pinHigh[pin] ? 1 : 0; }

#include "../test_garmin_views/view_stubs.h"   // first: pre-defines the guards of the display headers it replaces

#include <algorithm>
#include <memory>
#include <vector>

#include <unity.h>

#include "DeviceMode/PadelMode/PadelMode.h"
#include "DeviceMode/SquashMode/SquashMode.h"
#include "DeviceMode/VolleyballMode/VolleyballMode.h"
#include "Tournament/Rules/VolleyballRules.h"
#include "Utils.h"
#include "../common/check.h"
#include "../common/host_globals.h"

using Garmin::AckStatus;
using Garmin::CommandId;

void setUp() {}
void tearDown() {}

static const uint8_t PIN_A = 1, PIN_B = 2, PIN_C = 3, PIN_D = 4;
static const uint8_t SCREEN_MATCH_START = 0x11, SCREEN_INTRO = 0x12, SCREEN_PLAYING = 0x13;

static int g_ticks = 0;
static void countTick() { g_ticks++; }

static WatchCommand cmd(const CommandId id) {
    WatchCommand c = {};
    c.id = id;
    c.seq = 1;
    return c;
}

static WatchCommand toggle(const uint32_t uid) {
    WatchCommand c = cmd(CommandId::TogglePlayer);
    c.uid = uid;
    return c;
}

struct Rig {
    CRGB leds[Board::LED_COUNT];
    LedDisplay led{leds};
    BackDisplay back;
    EInkDisplay eink;
    RemoteInputManager remote{PIN_A, PIN_B, PIN_C, PIN_D};
    UserProfile anna{0, 0xA0000001, "ANNA", Colors::Red};
    UserProfile bob{1, 0xB0000002, "BOB", Colors::Blue};
    std::vector<UserProfile *> users{&anna, &bob};
    std::unique_ptr<DeviceMode> mode;
    volatile uint8_t triggered = 0;

    Rig() {
        g_fakeMillis = 100000;
        g_ticks = 0;
        std::fill(g_pinHigh, g_pinHigh + 8, 0);
        remote.setOnActionTaken(countTick);
    }

    // main.cpp's pass: poll, long C, then the mode.
    void tick(const uint32_t ms = 50) {
        g_fakeMillis += ms;
        remote.handleInput(triggered);
        if (remote.buttonC.takeLongPressIfPossible() && mode->goBack()) {
            remote.preventTriggerForMs();
        }
        mode->loop();
    }

    // A tap as the receiver delivers it: one HIGH of ~300 ms, an ISR edge at its start.
    void tap(const uint8_t pin) {
        g_pinHigh[pin] = 1;
        triggered = pin;
        tick();
        for (int i = 0; i < 6; i++) tick();
        g_pinHigh[pin] = 0;
    }

    uint8_t screen() const {
        WatchState state;
        mode->describeForWatch(state);
        uint8_t bytes[Garmin::STATE_HEADER + WatchState::MAX_DATA];
        state.build(0, AckStatus::Applied, 0, bytes);
        return bytes[0];
    }

    // PLAYING's temporary score at `offset` in the body: 11 (squash, volleyball) or the
    // padel point ladder at 13.
    void score(const uint8_t offset, uint8_t &left, uint8_t &right) const {
        WatchState state;
        mode->describeForWatch(state);
        uint8_t bytes[Garmin::STATE_HEADER + WatchState::MAX_DATA];
        state.build(0, AckStatus::Applied, 0, bytes);
        left = bytes[Garmin::STATE_HEADER + offset];
        right = bytes[Garmin::STATE_HEADER + offset + 1];
    }

    void toIntro() {
        tick();
        TEST_ASSERT_EQUAL(AckStatus::Applied, mode->handleWatchCommand(toggle(anna.getUid())));
        TEST_ASSERT_EQUAL(AckStatus::Applied, mode->handleWatchCommand(toggle(bob.getUid())));
        TEST_ASSERT_EQUAL(AckStatus::Applied, mode->handleWatchCommand(cmd(CommandId::StartTournament)));
        tick();
        TEST_ASSERT_EQUAL(AckStatus::Applied, mode->handleWatchCommand(cmd(CommandId::StartMatch)));
        tick();
        TEST_ASSERT_EQUAL_HEX8(SCREEN_INTRO, screen());
        g_ticks = 0;
    }
};

static void squash(Rig &rig) {
    rig.mode.reset(new SquashMode(rig.led, rig.back, rig.eink, rig.remote, [](DeviceModeState) {}, rig.users,
                                  std::function<void(CelebrationVariant)>()));
}

static void volleyball(Rig &rig) {
    rig.mode.reset(new VolleyballMode(rig.led, rig.back, rig.eink, rig.remote, [](DeviceModeState) {}, rig.users,
                                      std::make_unique<VolleyballRules>(), std::function<void(CelebrationVariant)>()));
}

static void padel(Rig &rig) {
    rig.mode.reset(new PadelMode(rig.led, rig.back, rig.eink, rig.remote, [](DeviceModeState) {}, rig.users,
                                 std::function<void(CelebrationVariant)>()));
}

static const uint8_t SCORE = 11, PADEL_POINT = 13;

static void expectScore(Rig &rig, const uint8_t left, const uint8_t right) {
    uint8_t l = 0xFF, r = 0xFF;
    rig.score(SCORE, l, r);
    TEST_ASSERT_EQUAL_UINT8(left, l);
    TEST_ASSERT_EQUAL_UINT8(right, r);
}

// A press during the wipe ends the intro, scores once on GamePlaying, ticks once.
static void pressScoresOnce(void (*build)(Rig &), const uint8_t pin, const uint8_t offset) {
    Rig rig;
    build(rig);
    rig.toIntro();
    const bool left = pin == PIN_A;

    rig.tap(pin);
    TEST_ASSERT_EQUAL_HEX8(SCREEN_PLAYING, rig.screen());
    uint8_t l = 0, r = 0;
    rig.score(offset, l, r);
    const uint8_t first = left ? l : r;
    TEST_ASSERT_TRUE(first > 0);
    TEST_ASSERT_EQUAL_UINT8(0, left ? r : l);
    TEST_ASSERT_EQUAL(1, g_ticks);
    for (int i = 0; i < 4; i++) rig.tick();
    TEST_ASSERT_EQUAL(1, g_ticks);

    // No 1 s block any more: the next tap counts once GamePlaying's 750 ms debounce is over.
    for (int i = 0; i < 6; i++) rig.tick();
    rig.tap(pin);
    rig.score(offset, l, r);
    TEST_ASSERT_TRUE((left ? l : r) > first);
    TEST_ASSERT_EQUAL_UINT8(0, left ? r : l);
    TEST_ASSERT_EQUAL(2, g_ticks);
}

// Started by the fob's D: MatchStartGame must not block the other buttons into the 1.2 s intro.
static void fobStartThenPress(void (*build)(Rig &), const uint8_t offset) {
    Rig rig;
    build(rig);
    rig.toIntro();
    rig.tap(PIN_D);   // D at 0:0 = undo, back to MatchStartGame
    for (int i = 0; i < 6 && rig.screen() != SCREEN_MATCH_START; i++) rig.tick();
    TEST_ASSERT_EQUAL_HEX8(SCREEN_MATCH_START, rig.screen());
    for (int i = 0; i < 12; i++) rig.tick();   // past D's own debounce
    g_ticks = 0;

    rig.tap(PIN_D);
    TEST_ASSERT_EQUAL_HEX8(SCREEN_INTRO, rig.screen());
    rig.tap(PIN_B);   // ~350 ms into the wipe
    TEST_ASSERT_EQUAL_HEX8(SCREEN_PLAYING, rig.screen());
    uint8_t l = 0xFF, r = 0;
    rig.score(offset, l, r);
    TEST_ASSERT_EQUAL_UINT8(0, l);
    TEST_ASSERT_TRUE(r > 0);
    TEST_ASSERT_EQUAL(2, g_ticks);
}

static void test_squash_fob_start_then_press() { fobStartThenPress(squash, SCORE); }
static void test_volleyball_fob_start_then_press() { fobStartThenPress(volleyball, SCORE); }
static void test_padel_fob_start_then_press() { fobStartThenPress(padel, PADEL_POINT); }

static void test_squash_a_scores_left() { pressScoresOnce(squash, PIN_A, SCORE); }
static void test_squash_b_scores_right() { pressScoresOnce(squash, PIN_B, SCORE); }
static void test_volleyball_a_scores_left() { pressScoresOnce(volleyball, PIN_A, SCORE); }
static void test_padel_b_scores_right() { pressScoresOnce(padel, PIN_B, PADEL_POINT); }
// C fires on release (deferToRelease); an undo at 0:0 lands back on MatchStartGame.
static void undoAtZeroLeaves(void (*build)(Rig &), const uint8_t pin) {
    Rig rig;
    build(rig);
    rig.toIntro();

    rig.tap(pin);
    for (int i = 0; i < 6 && rig.screen() != SCREEN_MATCH_START; i++) rig.tick();
    TEST_ASSERT_EQUAL_HEX8(SCREEN_MATCH_START, rig.screen());
    TEST_ASSERT_EQUAL(1, g_ticks);
}

static void test_squash_c_undo_at_zero() { undoAtZeroLeaves(squash, PIN_C); }
static void test_squash_d_undo_at_zero() { undoAtZeroLeaves(squash, PIN_D); }
static void test_padel_d_undo_at_zero() { undoAtZeroLeaves(padel, PIN_D); }

// No press: the timer ends the intro at 0:0, silently.
static void test_timer_hand_over_at_zero() {
    Rig rig;
    squash(rig);
    rig.toIntro();
    for (int i = 0; i < 60 && rig.screen() == SCREEN_INTRO; i++) rig.tick();
    TEST_ASSERT_EQUAL_HEX8(SCREEN_PLAYING, rig.screen());
    expectScore(rig, 0, 0);
    TEST_ASSERT_EQUAL(0, g_ticks);
}

// The timer hand-over still drops a press latched on the pass it requested the swap.
static void test_timer_hand_over_drops_latch() {
    Rig rig;
    squash(rig);
    rig.toIntro();
    for (int i = 0; i < 60 && !rig.mode->viewChangePending(); i++) {
        g_fakeMillis += 50;
        rig.mode->loop();
    }
    TEST_ASSERT_TRUE(rig.mode->viewChangePending());
    rig.remote.buttonA.trigger();   // latched between the request and the swap
    TEST_ASSERT_TRUE(rig.remote.anyActionPending());
    rig.mode->loop();
    TEST_ASSERT_EQUAL_HEX8(SCREEN_PLAYING, rig.screen());
    expectScore(rig, 0, 0);
    TEST_ASSERT_EQUAL(0, g_ticks);
}

// An overlay between the skip and the swap cancels the pass-on (main.cpp's clearLatches()).
static void test_overlay_cancels_pass_on() {
    Rig rig;
    squash(rig);
    rig.toIntro();
    rig.remote.buttonA.trigger();
    rig.mode->loop();
    TEST_ASSERT_TRUE(rig.mode->viewChangePending());
    rig.remote.clearLatches();
    rig.mode->loop();
    TEST_ASSERT_EQUAL_HEX8(SCREEN_PLAYING, rig.screen());
    expectScore(rig, 0, 0);
    TEST_ASSERT_EQUAL(0, g_ticks);
}

// One shot: only the next view swap keeps the latches.
static void test_pass_on_is_one_shot() {
    RemoteInputManager remote{PIN_A, PIN_B, PIN_C, PIN_D};
    remote.passLatchesToNextView();
    remote.buttonA.trigger();
    remote.clearLatchesForNewView();
    TEST_ASSERT_TRUE(remote.anyActionPending());
    remote.clearLatchesForNewView();
    TEST_ASSERT_FALSE(remote.anyActionPending());
}

// The watch SKIP keeps clearing a latched press, as before.
static void test_watch_skip_drops_latch() {
    Rig rig;
    squash(rig);
    rig.toIntro();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.mode->handleWatchCommand(cmd(CommandId::Skip)));
    rig.remote.buttonA.trigger();
    TEST_ASSERT_TRUE(rig.remote.anyActionPending());
    rig.mode->loop();
    TEST_ASSERT_EQUAL_HEX8(SCREEN_PLAYING, rig.screen());
    expectScore(rig, 0, 0);
    TEST_ASSERT_EQUAL(0, g_ticks);
}

// Two buttons latched in the intro: each acts once.
static void test_two_presses_each_once() {
    Rig rig;
    squash(rig);
    rig.toIntro();
    rig.remote.buttonA.trigger();
    rig.remote.buttonB.trigger();
    rig.mode->loop();
    rig.mode->loop();
    TEST_ASSERT_EQUAL_HEX8(SCREEN_PLAYING, rig.screen());
    expectScore(rig, 1, 1);
    TEST_ASSERT_EQUAL(2, g_ticks);
    for (int i = 0; i < 4; i++) rig.tick();
    expectScore(rig, 1, 1);
}

// main.cpp's long-C branch on the intro: back to MatchStartGame, no tick.
static void test_long_c_branch_in_intro_goes_back() {
    Rig rig;
    squash(rig);
    rig.toIntro();
    TEST_ASSERT_TRUE(rig.mode->goBack());
    rig.remote.preventTriggerForMs();
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(SCREEN_MATCH_START, rig.screen());
    TEST_ASSERT_EQUAL(0, g_ticks);
}

// A C hold begun in the intro is cut by the timer hand-over (clearLatches, as before #524):
// the intro is shorter than LONG_PRESS_MS, so it lands on GamePlaying at 0:0, silent.
static void test_c_hold_cut_by_timer_hand_over() {
    Rig rig;
    squash(rig);
    rig.toIntro();
    g_pinHigh[PIN_C] = 1;
    for (int i = 0; i < 50; i++) rig.tick();   // 2.5 s
    g_pinHigh[PIN_C] = 0;
    for (int i = 0; i < 4; i++) rig.tick();
    TEST_ASSERT_EQUAL_HEX8(SCREEN_PLAYING, rig.screen());
    expectScore(rig, 0, 0);
    TEST_ASSERT_EQUAL(0, g_ticks);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_squash_fob_start_then_press);
    RUN_TEST(test_volleyball_fob_start_then_press);
    RUN_TEST(test_padel_fob_start_then_press);
    RUN_TEST(test_squash_a_scores_left);
    RUN_TEST(test_squash_b_scores_right);
    RUN_TEST(test_volleyball_a_scores_left);
    RUN_TEST(test_padel_b_scores_right);
    RUN_TEST(test_squash_c_undo_at_zero);
    RUN_TEST(test_squash_d_undo_at_zero);
    RUN_TEST(test_padel_d_undo_at_zero);
    RUN_TEST(test_timer_hand_over_at_zero);
    RUN_TEST(test_timer_hand_over_drops_latch);
    RUN_TEST(test_overlay_cancels_pass_on);
    RUN_TEST(test_watch_skip_drops_latch);
    RUN_TEST(test_two_presses_each_once);
    RUN_TEST(test_pass_on_is_one_shot);
    RUN_TEST(test_long_c_branch_in_intro_goes_back);
    RUN_TEST(test_c_hold_cut_by_timer_hand_over);
    return UNITY_END();
}
