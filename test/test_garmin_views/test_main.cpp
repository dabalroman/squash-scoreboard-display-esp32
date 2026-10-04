// Watch dispatch and description through the real sport modes and views (#80, spec 10/11):
// commands reach the same action methods as the fob, a pending view swap answers BUSY,
// and each screen describes itself from the court side. Both boards.
#include "view_stubs.h"   // first: pre-defines the guards of the display headers it replaces

#include <memory>
#include <vector>

#include <unity.h>

#include "DeviceMode/ModeSwitcherMode/ModeSwitchingMode.h"
#include "DeviceMode/PadelMode/PadelMode.h"
#include "DeviceMode/SquashMode/SquashMode.h"
#include "DeviceMode/VolleyballMode/VolleyballMode.h"
#include "Tournament/Rules/ShortVolleyballRules.h"
#include "DeviceMode/WatchSupport.h"
#include "../common/check.h"
#include "../common/host_globals.h"

using Garmin::AckStatus;
using Garmin::CommandId;

void setUp() {}
void tearDown() {}

static const uint32_t UID_ANNA = 0xA0000001;
static const uint32_t UID_BOB = 0xB0000002;
static const uint32_t UID_CEZ = 0xC0000003;
static const uint32_t UID_NOBODY = 0x12345678;

static WatchCommand cmd(const CommandId id, const uint8_t side = 0) {
    WatchCommand c = {};
    c.id = id;
    c.seq = 1;
    c.side = side;
    return c;
}

static WatchCommand toggle(const uint32_t uid) {
    WatchCommand c = cmd(CommandId::TogglePlayer);
    c.uid = uid;
    return c;
}

static WatchCommand pair(const uint32_t left, const uint32_t right) {
    WatchCommand c = cmd(CommandId::SetPair);
    c.leftUid = left;
    c.rightUid = right;
    return c;
}

static WatchCommand focus(const uint32_t target) {
    WatchCommand c = cmd(CommandId::Focus);
    c.target = target;
    return c;
}

static WatchCommand sport(const uint8_t id) {
    WatchCommand c = cmd(CommandId::SelectSport);
    c.sport = id;
    return c;
}

static uint32_t u32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/** The body as the service would send it, with no ack and roster version 0. */
struct Body {
    uint8_t bytes[Garmin::STATE_HEADER + WatchState::MAX_DATA];
    uint8_t size;

    explicit Body(const WatchState &state) {
        size = state.build(0, AckStatus::Applied, 0, bytes);
    }

    uint8_t screen() const { return bytes[0]; }
    const uint8_t *data() const { return bytes + Garmin::STATE_HEADER; }
};

/** Three profiles, a mode on the real LED display, the fob idle. */
struct Rig {
    CRGB leds[Board::LED_COUNT];
    LedDisplay led{leds};
    BackDisplay back;
    EInkDisplay eink;
    RemoteInputManager remote{1, 2, 3, 4};
    UserProfile anna{0, UID_ANNA, "ANNA", Colors::Red};
    UserProfile bob{1, UID_BOB, "BOB", Colors::Blue};
    UserProfile cez{2, UID_CEZ, "CEZ", Colors::Green};
    std::vector<UserProfile *> users{&anna, &bob, &cez};
    std::vector<DeviceModeState> requested;
    std::unique_ptr<DeviceMode> mode;

    std::function<void(DeviceModeState)> onModeChange() {
        return [this](const DeviceModeState state) { requested.push_back(state); };
    }

    AckStatus send(const WatchCommand &c) { return mode->handleWatchCommand(c); }

    void tick(const uint32_t ms = 50) {
        g_fakeMillis += ms;
        mode->loop();
    }

    Body describe() const {
        WatchState state;
        mode->describeForWatch(state);
        return Body(state);
    }

    // Choose Anna and Bob, start, land on MatchStart.
    void toMatchStart() {
        tick();
        TEST_ASSERT_EQUAL(AckStatus::Applied, send(toggle(UID_ANNA)));
        TEST_ASSERT_EQUAL(AckStatus::Applied, send(toggle(UID_BOB)));
        TEST_ASSERT_EQUAL(AckStatus::Applied, send(cmd(CommandId::StartTournament)));
        tick();
    }

    void toPlaying() {
        toMatchStart();
        TEST_ASSERT_EQUAL(AckStatus::Applied, send(cmd(CommandId::StartMatch)));
        tick();
        TEST_ASSERT_EQUAL(AckStatus::Applied, send(cmd(CommandId::Skip)));
        tick();
        TEST_ASSERT_EQUAL_HEX8(0x13, describe().screen());
    }

    // Past the 4 s commit window.
    void commit() { tick(4100); }
};

static void squashRig(Rig &rig) {
    g_fakeMillis = 100000;
    rig.mode.reset(new SquashMode(rig.led, rig.back, rig.eink, rig.remote, rig.onModeChange(), rig.users,
                                  std::function<void(CelebrationVariant)>()));
}

static void padelRig(Rig &rig) {
    g_fakeMillis = 100000;
    rig.mode.reset(new PadelMode(rig.led, rig.back, rig.eink, rig.remote, rig.onModeChange(), rig.users,
                                 std::function<void(CelebrationVariant)>()));
}

// --- routing ----------------------------------------------------------------------------

static void test_route_pending_is_busy_even_for_back() {
    int backs = 0, views = 0;
    auto goBack = [&] { backs++; return true; };
    auto toView = [&] { views++; return AckStatus::Applied; };

    TEST_ASSERT_EQUAL(AckStatus::Busy, WatchSupport::route(cmd(CommandId::Back), true, goBack, toView));
    TEST_ASSERT_EQUAL(AckStatus::Busy, WatchSupport::route(cmd(CommandId::Score), true, goBack, toView));
    TEST_ASSERT_EQUAL(0, backs + views);

    TEST_ASSERT_EQUAL(AckStatus::Applied, WatchSupport::route(cmd(CommandId::Back), false, goBack, toView));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen,
                      WatchSupport::route(cmd(CommandId::Back), false, [] { return false; }, toView));
    TEST_ASSERT_EQUAL(AckStatus::Applied, WatchSupport::route(cmd(CommandId::Score), false, goBack, toView));
    TEST_ASSERT_EQUAL(1, backs);
    TEST_ASSERT_EQUAL(1, views);
}

// --- squash ---------------------------------------------------------------------------

static void test_squash_choose_players() {
    Rig rig;
    squashRig(rig);

    // Constructed, view not built until the first loop().
    TEST_ASSERT_EQUAL(AckStatus::Busy, rig.send(toggle(UID_ANNA)));
    rig.tick();

    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(toggle(UID_NOBODY)));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(cmd(CommandId::StartTournament)));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(cmd(CommandId::Score)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(toggle(UID_ANNA)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(toggle(UID_CEZ)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(toggle(UID_CEZ)));   // and out again
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(cmd(CommandId::StartTournament)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(toggle(UID_BOB)));

    const Body body = rig.describe();
    TEST_ASSERT_EQUAL(16, body.size);
    TEST_ASSERT_EQUAL_HEX8(0x10, body.screen());
    TEST_ASSERT_EQUAL_HEX32(0x3, u32(body.data()));

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartTournament)));
    // MatchStartGame is pending: the choose view is the outgoing one.
    TEST_ASSERT_EQUAL(AckStatus::Busy, rig.send(toggle(UID_CEZ)));
    TEST_ASSERT_EQUAL(AckStatus::Busy, rig.send(cmd(CommandId::Back)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x11, rig.describe().screen());

    // BACK walks the long-C chain.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Back)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x10, rig.describe().screen());
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Back)));
    TEST_ASSERT_EQUAL(1, rig.requested.size());
    TEST_ASSERT_EQUAL(DeviceModeState::ModeSwitchingMode, rig.requested[0]);
}

static void test_squash_match_start() {
    Rig rig;
    squashRig(rig);
    rig.toMatchStart();

    Body body = rig.describe();
    TEST_ASSERT_EQUAL(20, body.size);
    TEST_ASSERT_EQUAL_HEX32(0x3, u32(body.data()));
    TEST_ASSERT_EQUAL_HEX32(UID_ANNA, u32(body.data() + 4));
    TEST_ASSERT_EQUAL_HEX32(UID_BOB, u32(body.data() + 8));

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::SwapSides)));
    body = rig.describe();
    TEST_ASSERT_EQUAL_HEX32(UID_BOB, u32(body.data() + 4));
    TEST_ASSERT_EQUAL_HEX32(UID_ANNA, u32(body.data() + 8));

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(pair(UID_ANNA, UID_BOB)));
    body = rig.describe();
    TEST_ASSERT_EQUAL_HEX32(UID_ANNA, u32(body.data() + 4));
    TEST_ASSERT_EQUAL_HEX32(UID_BOB, u32(body.data() + 8));

    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(pair(UID_ANNA, UID_ANNA)));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(pair(UID_ANNA, UID_CEZ)));   // not selected
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(cmd(CommandId::Skip)));

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartMatch)));
    TEST_ASSERT_EQUAL(AckStatus::Busy, rig.send(cmd(CommandId::SwapSides)));
    rig.tick();

    body = rig.describe();
    TEST_ASSERT_EQUAL(16, body.size);
    TEST_ASSERT_EQUAL_HEX8(0x12, body.screen());
    TEST_ASSERT_EQUAL_HEX32(UID_ANNA, u32(body.data()));
    TEST_ASSERT_EQUAL_HEX32(UID_BOB, u32(body.data() + 4));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(cmd(CommandId::Score)));
}

static void test_squash_playing_score_undo_and_game_ball() {
    Rig rig;
    squashRig(rig);
    rig.toPlaying();

    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(cmd(CommandId::Score, 2)));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(cmd(CommandId::Undo, 7)));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(cmd(CommandId::Skip)));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(cmd(CommandId::Back)));   // never discards a game

    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 0)));
    }
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 1)));

    // Uncommitted: the temporary score, and no game ball yet (it reads the committed one).
    Body body = rig.describe();
    TEST_ASSERT_EQUAL(21, body.size);
    const uint8_t uncommitted[13] = {0x01, 0x00, 0x00, 0xA0, 0x02, 0x00, 0x00, 0xB0, 0x01, 0, 0, 10, 1};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(uncommitted, body.data(), 13);

    rig.commit();
    body = rig.describe();
    const uint8_t gameBall[13] = {0x01, 0x00, 0x00, 0xA0, 0x02, 0x00, 0x00, 0xB0, 0x04, 0, 0, 10, 1};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(gameBall, body.data(), 13);

    // UNDO right takes Bob's point back; not 0:0, so the game stays.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 1)));
    TEST_ASSERT_EQUAL_HEX8(0x01 | 0x04, rig.describe().data()[8]);
    TEST_ASSERT_EQUAL(0, rig.describe().data()[12]);
}

// The winning commit frees the Game (Match::finishGame) inside GamePlaying's handleInput; the
// mode must not render that view again. Before the fix this faulted on the host heap.
static void test_squash_winning_commit_through_loop() {
    Rig rig;
    squashRig(rig);
    rig.toPlaying();
    for (int i = 0; i < 11; i++) {
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 0)));
    }
    rig.commit();
    TEST_ASSERT_EQUAL(AckStatus::Busy, rig.send(cmd(CommandId::Skip)));   // swap pending
    TEST_ASSERT_TRUE(rig.mode->viewChangePending());   // main.cpp skips publishing now
    rig.describe();   // must not read the freed Game either
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x14, rig.describe().screen());
}

// Padel's set-winning commit takes the same path: a gem lands when its rallies commit, and
// the sixth one finishes the set.
static void test_padel_set_win_through_loop() {
    Rig rig;
    padelRig(rig);
    rig.toPlaying();
    for (int gem = 0; gem < 6; gem++) {
        for (int i = 0; i < 4; i++) {
            TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 0)));
        }
        rig.commit();
    }
    TEST_ASSERT_TRUE(rig.mode->viewChangePending());
    rig.describe();
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x14, rig.describe().screen());
}

// Built on the model and the views directly, so the result bytes are checked in isolation.
static void test_celebration_and_game_over_describe() {
    g_fakeMillis = 100000;
    UserProfile anna(0, UID_ANNA, "ANNA", Colors::Red);
    UserProfile bob(1, UID_BOB, "BOB", Colors::Blue);
    Tournament tournament(std::unique_ptr<Rules>(new SquashRules()));
    tournament.addPlayer(anna);
    tournament.addPlayer(bob);

    // Bob on the left, so A/B and left/right differ.
    Match &match = tournament.chooseMatchBetween(bob, anna);
    Game *game = match.createGame();
    for (int i = 0; i < 11; i++) {
        game->scorePoint(GameSide::b);   // the right court player: Anna
    }
    game->commit();
    match.finishGame();

    const uint8_t bajgiel[14] = {0x02, 0x00, 0x00, 0xB0, 0x01, 0x00, 0x00, 0xA0, 1, 0, 11, 0, 1, 1};
    std::vector<SquashModeState> states;
    auto onState = [&](const SquashModeState s) { states.push_back(s); };

    GameCelebrationView<SquashModeState> celebration(tournament, onState, "", false);
    WatchState state;
    celebration.describeForWatch(state);
    const Body body(state);
    TEST_ASSERT_EQUAL(22, body.size);
    TEST_ASSERT_EQUAL_HEX8(0x14, body.screen());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bajgiel, body.data(), 14);

    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, celebration.handleWatchCommand(cmd(CommandId::NextGame)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, celebration.handleWatchCommand(cmd(CommandId::Skip)));
    TEST_ASSERT_EQUAL(1, states.size());
    TEST_ASSERT_EQUAL(SquashModeState::GameOver, states[0]);

    SquashGameOverView over(tournament, onState);
    over.describeForWatch(state);
    const Body overBody(state);
    TEST_ASSERT_EQUAL_HEX8(0x15, overBody.screen());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bajgiel, overBody.data(), 14);

    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, over.handleWatchCommand(cmd(CommandId::Skip)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, over.handleWatchCommand(cmd(CommandId::NextGame)));
    TEST_ASSERT_EQUAL(2, states.size());
    TEST_ASSERT_EQUAL(SquashModeState::MatchStartGame, states[1]);
}

static void test_squash_court_side_after_swap() {
    Rig rig;
    squashRig(rig);
    rig.toMatchStart();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::SwapSides)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartMatch)));
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Skip)));
    rig.tick();

    // Side 0 is Bob's now: he is on the left.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 0)));
    const Body body = rig.describe();
    TEST_ASSERT_EQUAL_HEX32(UID_BOB, u32(body.data()));
    TEST_ASSERT_EQUAL_HEX32(UID_ANNA, u32(body.data() + 4));
    TEST_ASSERT_EQUAL(1, body.data()[11]);
    TEST_ASSERT_EQUAL(0, body.data()[12]);
}

static void test_squash_undo_at_zero_leaves_for_match_start() {
    Rig rig;
    squashRig(rig);
    rig.toPlaying();

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 0)));
    TEST_ASSERT_EQUAL(AckStatus::Busy, rig.send(cmd(CommandId::Score, 0)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x11, rig.describe().screen());
}

// --- padel ----------------------------------------------------------------------------

static void rallies(Rig &rig, const uint8_t side, const int count) {
    for (int i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, side)));
    }
    rig.commit();
}

static void test_padel_playing_ladder_and_gem_ball() {
    Rig rig;
    padelRig(rig);
    rig.toPlaying();

    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(cmd(CommandId::Score, 2)));

    // 40:15 committed: gem ball left, points 3 and 1.
    rallies(rig, 0, 3);
    rallies(rig, 1, 1);
    Body body = rig.describe();
    TEST_ASSERT_EQUAL(23, body.size);
    const uint8_t fortyFifteen[15] = {0x01, 0x00, 0x00, 0xA0, 0x02, 0x00, 0x00, 0xB0, 0x04, 0, 0, 0, 0, 3, 1};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fortyFifteen, body.data(), 15);

    // The gem: 1:0 in gems, a fresh 0:0 ladder.
    rallies(rig, 0, 1);
    body = rig.describe();
    const uint8_t oneLove[15] = {0x01, 0x00, 0x00, 0xA0, 0x02, 0x00, 0x00, 0xB0, 0x00, 0, 0, 1, 0, 0, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(oneLove, body.data(), 15);

    // UNDO at 0:0 of a new gem steps back into the last one (40:15 again), it does not leave.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 0)));
    body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x13, body.screen());
    TEST_ASSERT_EQUAL(0, body.data()[11]);
    TEST_ASSERT_EQUAL(3, body.data()[13]);
    TEST_ASSERT_EQUAL(1, body.data()[14]);
}

static void test_padel_tiebreak_flag_and_raw_points() {
    Rig rig;
    padelRig(rig);
    rig.toPlaying();

    for (int gem = 0; gem < 6; gem++) {
        rallies(rig, 0, 4);
        rallies(rig, 1, 4);
    }
    rallies(rig, 0, 5);

    const Body body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x02, body.data()[8]);   // TIEBREAK, no gem ball at 5:0 of 7
    TEST_ASSERT_EQUAL(6, body.data()[11]);
    TEST_ASSERT_EQUAL(6, body.data()[12]);
    TEST_ASSERT_EQUAL(5, body.data()[13]);
    TEST_ASSERT_EQUAL(0, body.data()[14]);

    rallies(rig, 0, 1);
    TEST_ASSERT_EQUAL_HEX8(0x02 | 0x04, rig.describe().data()[8]);   // set ball
}

static void test_padel_undo_with_nothing_to_step_back_leaves() {
    Rig rig;
    padelRig(rig);
    rig.toPlaying();

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 1)));
    TEST_ASSERT_EQUAL(AckStatus::Busy, rig.send(cmd(CommandId::Undo, 1)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x11, rig.describe().screen());
}

// --- volleyball: the third copy of the views (also short volleyball) ----------------------

static void test_volleyball_scores_through_the_same_path() {
    Rig rig;
    g_fakeMillis = 100000;
    rig.mode.reset(new VolleyballMode(rig.led, rig.back, rig.eink, rig.remote, rig.onModeChange(), rig.users,
                                      std::unique_ptr<Rules>(new ShortVolleyballRules()),
                                      std::function<void(CelebrationVariant)>()));
    rig.toPlaying();

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 1)));
    const Body body = rig.describe();
    TEST_ASSERT_EQUAL(21, body.size);
    TEST_ASSERT_EQUAL_HEX8(0x01, body.data()[8]);
    TEST_ASSERT_EQUAL(0, body.data()[11]);
    TEST_ASSERT_EQUAL(1, body.data()[12]);

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 1)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 0)));   // 0:0 -> MatchStart
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x11, rig.describe().screen());
}

// --- menu -----------------------------------------------------------------------------

static void test_menu_select_sport_and_describe() {
    Rig rig;
    g_fakeMillis = 100000;
    BatterySensor sensor;
    BatteryMonitor battery(sensor);
    rig.mode.reset(new ModeSwitchingMode(rig.led, rig.back, rig.eink, rig.remote, rig.onModeChange(), battery));

    const Body body = rig.describe();
    TEST_ASSERT_EQUAL(14, body.size);
    const uint8_t menu[6] = {0x04, 4, 0x04, 0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(menu, body.data(), 6);

    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(sport(0)));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(sport(9)));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(cmd(CommandId::Score)));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(cmd(CommandId::Back)));   // a root
    TEST_ASSERT_EQUAL(0, rig.requested.size());

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(sport(0x03)));
    TEST_ASSERT_EQUAL(1, rig.requested.size());
    TEST_ASSERT_EQUAL(DeviceModeState::ShortVolleyballMode, rig.requested[0]);
}

// --- FOCUS (spec 10.2 0x0E): the fob's A/B cursor move, nothing else ----------------------

static void menuRig(Rig &rig, BatteryMonitor &battery) {
    g_fakeMillis = 100000;
    rig.mode.reset(new ModeSwitchingMode(rig.led, rig.back, rig.eink, rig.remote, rig.onModeChange(), battery));
    rig.tick();
}

// One fresh frame of each rig at the same instant: the shim's FastLED.clear() wipes only a
// registered buffer, and the rigs share the fake clock (blink phase).
static void renderNow(Rig &rig) {
    FastLED.registerBuffer(rig.leds, Board::LED_COUNT);
    rig.mode->restoreView();
    rig.tick(0);
}

static bool sameLeds(Rig &a, Rig &b) {
    renderNow(a);
    renderNow(b);
    FastLED.registerBuffer(nullptr, 0);
    return memcmp(a.leds, b.leds, sizeof(a.leds)) == 0;
}

static void test_menu_focus() {
    BatterySensor sensor;
    BatteryMonitor battery(sensor);
    Rig rig;
    menuRig(rig, battery);

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(focus(0x01)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x01, rig.describe().data()[0]);
    TEST_ASSERT_EQUAL(0, rig.requested.size());   // a cursor move never selects

    // The same frame a fob B press from padel draws.
    Rig fob;
    menuRig(fob, battery);
    fob.remote.buttonB.trigger();
    fob.tick();
    TEST_ASSERT_EQUAL_HEX8(0x01, fob.describe().data()[0]);
    TEST_ASSERT_TRUE_MESSAGE(sameLeds(rig, fob), "FOCUS and fob B light the same LEDs");

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(focus(0x01)));   // already there
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(focus(0)));      // PROFILE / CONFIG are not focusable
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(focus(9)));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(focus(0x101)));  // only the low byte would match squash
    TEST_ASSERT_EQUAL_HEX8(0x01, rig.describe().data()[0]);

    // A fob scroll onto a non-sport row reports cursor 0; FOCUS takes it back.
    rig.tick(600);
    rig.remote.buttonA.trigger();
    rig.tick();
    rig.tick(600);
    rig.remote.buttonA.trigger();
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x00, rig.describe().data()[0]);
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(focus(0x03)));
    TEST_ASSERT_EQUAL_HEX8(0x03, rig.describe().data()[0]);
    TEST_ASSERT_EQUAL(0, rig.requested.size());
}

static const uint8_t *cursorOf(const Body &body) {
    return body.data() + 4;
}

// START, ANNA, BOB, CEZ: the cursor moves, the selection never does.
static void checkChooseFocus(Rig &rig) {
    rig.tick();
    Body body = rig.describe();
    TEST_ASSERT_EQUAL(16, body.size);
    TEST_ASSERT_EQUAL_HEX32(0, u32(cursorOf(body)));

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(focus(UID_BOB)));
    body = rig.describe();
    TEST_ASSERT_EQUAL_HEX32(UID_BOB, u32(cursorOf(body)));
    TEST_ASSERT_EQUAL_HEX32(0, u32(body.data()));

    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(focus(UID_NOBODY)));
    TEST_ASSERT_EQUAL_HEX32(UID_BOB, u32(cursorOf(rig.describe())));

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(focus(0)));
    TEST_ASSERT_EQUAL_HEX32(0, u32(cursorOf(rig.describe())));
    TEST_ASSERT_EQUAL(0, rig.requested.size());
}

static void test_choose_players_focus() {
    Rig rig;
    squashRig(rig);
    checkChooseFocus(rig);

    // FOCUS onto BOB draws what two fob B presses from START draw.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(focus(UID_BOB)));
    rig.tick();
    Rig fob;
    squashRig(fob);
    fob.tick();
    fob.remote.buttonB.trigger();
    fob.tick();
    fob.tick(600);   // past the fob's debounce
    fob.remote.buttonB.trigger();
    fob.tick();
    TEST_ASSERT_EQUAL_HEX32(UID_BOB, u32(cursorOf(fob.describe())));
    TEST_ASSERT_TRUE_MESSAGE(sameLeds(rig, fob), "FOCUS and fob B light the same LEDs");

    // Fob A from START wraps to the last profile, and the state reports it.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(focus(0)));
    rig.tick(600);
    rig.remote.buttonA.trigger();
    rig.tick();
    TEST_ASSERT_EQUAL_HEX32(UID_CEZ, u32(cursorOf(rig.describe())));

    // A cosmetic command, but only on the list.
    rig.toMatchStart();
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(focus(UID_ANNA)));
}

static void test_choose_players_focus_padel_and_volleyball() {
    Rig padel;
    padelRig(padel);
    checkChooseFocus(padel);

    Rig volley;
    g_fakeMillis = 100000;
    volley.mode.reset(new VolleyballMode(volley.led, volley.back, volley.eink, volley.remote, volley.onModeChange(),
                                         volley.users, std::unique_ptr<Rules>(new ShortVolleyballRules()),
                                         std::function<void(CelebrationVariant)>()));
    checkChooseFocus(volley);
}

static void test_focus_wrong_screen_while_playing() {
    Rig rig;
    squashRig(rig);
    rig.toPlaying();
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(focus(0)));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(focus(UID_ANNA)));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_route_pending_is_busy_even_for_back);
    RUN_TEST(test_squash_choose_players);
    RUN_TEST(test_squash_match_start);
    RUN_TEST(test_squash_playing_score_undo_and_game_ball);
    RUN_TEST(test_squash_winning_commit_through_loop);
    RUN_TEST(test_padel_set_win_through_loop);
    RUN_TEST(test_celebration_and_game_over_describe);
    RUN_TEST(test_squash_court_side_after_swap);
    RUN_TEST(test_squash_undo_at_zero_leaves_for_match_start);
    RUN_TEST(test_padel_playing_ladder_and_gem_ball);
    RUN_TEST(test_padel_tiebreak_flag_and_raw_points);
    RUN_TEST(test_padel_undo_with_nothing_to_step_back_leaves);
    RUN_TEST(test_volleyball_scores_through_the_same_path);
    RUN_TEST(test_menu_select_sport_and_describe);
    RUN_TEST(test_menu_focus);
    RUN_TEST(test_choose_players_focus);
    RUN_TEST(test_choose_players_focus_padel_and_volleyball);
    RUN_TEST(test_focus_wrong_screen_while_playing);
    return UNITY_END();
}
