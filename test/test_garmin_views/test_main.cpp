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
#include "DeviceMode/TournamentSync.h"
#include "Display/Overlay.h"
#include "Tournament/Rules/VolleyballRules.h"
#include "Tournament/Rules/ShortVolleyballRules.h"
#include "DeviceMode/WatchSupport.h"
#include "../common/check.h"
#include "../common/host_globals.h"

using Garmin::AckStatus;
using Garmin::CommandId;

// Modes restore the last START selection (SelectionStore): every test starts without one.
void setUp() { HostNvs::reset(); }
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
    // ACK_CHANGES from a connection whose STATUS read saw the running tournament's log.
    AckStatus ack(uint32_t tid, uint16_t upTo);

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
    int backs = 0, edits = 0, syncs = 0, views = 0;
    auto goBack = [&] { backs++; return AckStatus::Applied; };
    auto editGames = [&] { edits++; return AckStatus::Invalid; };
    auto toTournament = [&] { syncs++; return AckStatus::WrongScreen; };
    auto toView = [&] { views++; return AckStatus::Applied; };

    TEST_ASSERT_EQUAL(AckStatus::Busy, WatchSupport::route(cmd(CommandId::Back), true, goBack, editGames, toTournament, toView));
    TEST_ASSERT_EQUAL(AckStatus::Busy, WatchSupport::route(cmd(CommandId::Score), true, goBack, editGames, toTournament, toView));
    TEST_ASSERT_EQUAL(AckStatus::Busy,
                      WatchSupport::route(cmd(CommandId::AddOfflineGame), true, goBack, editGames, toTournament, toView));
    TEST_ASSERT_EQUAL(0, backs + edits + views);

    TEST_ASSERT_EQUAL(AckStatus::Applied, WatchSupport::route(cmd(CommandId::Back), false, goBack, editGames, toTournament, toView));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen,
                      WatchSupport::route(cmd(CommandId::Back), false, [] { return AckStatus::WrongScreen; }, editGames, toTournament, toView));
    TEST_ASSERT_EQUAL(AckStatus::Applied, WatchSupport::route(cmd(CommandId::Score), false, goBack, editGames, toTournament, toView));
    // Offline games never reach the view.
    TEST_ASSERT_EQUAL(AckStatus::Invalid,
                      WatchSupport::route(cmd(CommandId::AddOfflineGame), false, goBack, editGames, toTournament, toView));
    TEST_ASSERT_EQUAL(AckStatus::Invalid,
                      WatchSupport::route(cmd(CommandId::RemoveOfflineGame), false, goBack, editGames, toTournament, toView));
    TEST_ASSERT_EQUAL(AckStatus::Busy,
                      WatchSupport::route(cmd(CommandId::AckChanges), true, goBack, editGames, toTournament, toView));
    // Tournament commands never reach the view either.
    const CommandId syncIds[] = {CommandId::BeginTournament, CommandId::AckChanges, CommandId::SetSide};
    for (const CommandId id : syncIds) {
        TEST_ASSERT_EQUAL(AckStatus::WrongScreen,
                          WatchSupport::route(cmd(id), false, goBack, editGames, toTournament, toView));
    }
    TEST_ASSERT_EQUAL(1, backs);
    TEST_ASSERT_EQUAL(2, edits);
    TEST_ASSERT_EQUAL(3, syncs);
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

// The START selection comes back on the next mode entry, per sport, minus deleted profiles.
static void test_selection_restored_on_mode_entry() {
    {
        Rig rig;
        squashRig(rig);
        rig.tick();
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(toggle(UID_CEZ)));
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(toggle(UID_ANNA)));
        TEST_ASSERT_EQUAL(0, HostNvs::writeCount());  // toggling writes nothing
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartTournament)));
        rig.tick();
        TEST_ASSERT_EQUAL(1, HostNvs::writeCount());
    }
    {
        Rig rig;
        squashRig(rig);
        rig.tick();
        TEST_ASSERT_EQUAL_HEX8(0x10, rig.describe().screen());
        TEST_ASSERT_EQUAL_HEX32(0x5, u32(rig.describe().data()));
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartTournament)));
        rig.tick();
        TEST_ASSERT_EQUAL(1, HostNvs::writeCount());  // same selection: no rewrite
    }
    {
        Rig rig;
        padelRig(rig);
        rig.tick();
        TEST_ASSERT_EQUAL_HEX32(0, u32(rig.describe().data()));
    }
    {
        Rig rig;
        rig.users = {&rig.anna, &rig.bob};  // Cez deleted since
        squashRig(rig);
        rig.tick();
        TEST_ASSERT_EQUAL_HEX32(0x1, u32(rig.describe().data()));
        TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(cmd(CommandId::StartTournament)));
    }
}

static void test_squash_match_start() {
    Rig rig;
    squashRig(rig);
    rig.toMatchStart();

    Body body = rig.describe();
    TEST_ASSERT_EQUAL(21, body.size);
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
    TEST_ASSERT_EQUAL(17, body.size);
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
    // The newest point is Bob's: LAST_KNOWN | LAST_RIGHT.
    Body body = rig.describe();
    TEST_ASSERT_EQUAL(21, body.size);
    const uint8_t uncommitted[13] = {0x01, 0x00, 0x00, 0xA0, 0x02, 0x00, 0x00, 0xB0, 0x31, 0, 0, 10, 1};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(uncommitted, body.data(), 13);

    rig.commit();
    body = rig.describe();
    const uint8_t gameBall[13] = {0x01, 0x00, 0x00, 0xA0, 0x02, 0x00, 0x00, 0xB0, 0x34, 0, 0, 10, 1};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(gameBall, body.data(), 13);

    // UNDO right takes Bob's point back; not 0:0, so the game stays. Anna's is the newest now.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 1)));
    TEST_ASSERT_EQUAL_HEX8(0x01 | 0x04 | 0x10, rig.describe().data()[8]);
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
    // Games are recorded only in a running tournament.
    tournament.start(static_cast<uint8_t>(Garmin::SportId::Squash));

    // Bob on the left, so A/B and left/right differ.
    Match &match = tournament.chooseMatchBetween(bob, anna);
    Game *game = match.createGame();
    for (int i = 0; i < 11; i++) {
        game->scorePoint(GameSide::b);   // the right court player: Anna
    }
    game->commit();
    match.finishGame();

    const uint8_t bajgiel[15] = {0x02, 0x00, 0x00, 0xB0, 0x01, 0x00, 0x00, 0xA0, 1, 0, 11, 0, 1, 1, 0};
    std::vector<SquashModeState> states;
    auto onState = [&](const SquashModeState s) { states.push_back(s); };

    GameCelebrationView<SquashModeState> celebration(tournament, onState, "", false);
    WatchState state;
    celebration.describeForWatch(state);
    const Body body(state);
    TEST_ASSERT_EQUAL(23, body.size);
    TEST_ASSERT_EQUAL_HEX8(0x14, body.screen());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bajgiel, body.data(), 15);

    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, celebration.handleWatchCommand(cmd(CommandId::NextGame)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, celebration.handleWatchCommand(cmd(CommandId::Skip)));
    TEST_ASSERT_EQUAL(1, states.size());
    TEST_ASSERT_EQUAL(SquashModeState::GameOver, states[0]);

    SquashGameOverView over(tournament.getActiveMatch(), onState);
    over.describeForWatch(state);
    const Body overBody(state);
    TEST_ASSERT_EQUAL_HEX8(0x15, overBody.screen());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bajgiel, overBody.data(), 15);

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

// --- SIDES_SWAPPED: the pair against its orientation at its first game (spec 11.3) -------

static const uint8_t SIDES_SWAPPED = 0x40;

// From MATCH_START; the INTRO flags byte.
static uint8_t startToIntro(Rig &rig) {
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartMatch)));
    rig.tick();
    const Body body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x12, body.screen());
    TEST_ASSERT_EQUAL(17, body.size);
    return body.data()[8];
}

// From INTRO; SIDES_SWAPPED in the PLAYING flags.
static uint8_t skipToPlaying(Rig &rig) {
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Skip)));
    rig.tick();
    const Body body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x13, body.screen());
    return body.data()[8] & SIDES_SWAPPED;
}

// `side` wins the game point by point, then through CELEBRATION and GAME_OVER (both checked
// for `flags`) back to MATCH_START.
static void winToMatchStart(Rig &rig, const uint8_t side, const uint8_t flags) {
    for (int i = 0; i < 200 && rig.describe().screen() == 0x13; i++) {
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, side)));
        rig.commit();
        if (rig.mode->viewChangePending()) rig.tick();
    }
    Body body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x14, body.screen());
    TEST_ASSERT_EQUAL(23, body.size);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(flags, body.data()[14], "CELEBRATION flags");

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Skip)));
    rig.tick();
    body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x15, body.screen());
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(flags, body.data()[14], "GAME_OVER flags");

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::NextGame)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x11, rig.describe().screen());
}

// MATCH_START's flags byte (body 21).
static uint8_t matchStartFlags(Rig &rig) {
    const Body body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x11, body.screen());
    TEST_ASSERT_EQUAL(21, body.size);
    return body.data()[12];
}

static void sidesSwappedAfterFirstGame(Rig &rig) {
    rig.toMatchStart();
    // A swap before the first game is the start orientation, not a swap.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::SwapSides)));
    TEST_ASSERT_EQUAL_HEX8(0, matchStartFlags(rig));
    TEST_ASSERT_EQUAL_HEX8(0, startToIntro(rig));
    TEST_ASSERT_EQUAL_HEX8(0, skipToPlaying(rig));
    winToMatchStart(rig, 0, 0);
    TEST_ASSERT_EQUAL_HEX8(0, matchStartFlags(rig));

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::SwapSides)));
    TEST_ASSERT_EQUAL_HEX8(SIDES_SWAPPED, matchStartFlags(rig));
    TEST_ASSERT_EQUAL_HEX8(SIDES_SWAPPED, startToIntro(rig));
    TEST_ASSERT_EQUAL_HEX8(SIDES_SWAPPED, skipToPlaying(rig));
    winToMatchStart(rig, 1, SIDES_SWAPPED);
    TEST_ASSERT_EQUAL_HEX8(SIDES_SWAPPED, matchStartFlags(rig));

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::SwapSides)));
    TEST_ASSERT_EQUAL_HEX8(0, matchStartFlags(rig));
    TEST_ASSERT_EQUAL_HEX8(0, startToIntro(rig));
    TEST_ASSERT_EQUAL_HEX8(0, skipToPlaying(rig));
}

static void test_squash_sides_swapped_latch() {
    Rig rig;
    squashRig(rig);
    sidesSwappedAfterFirstGame(rig);
}

static void test_padel_sides_swapped_latch() {
    Rig rig;
    padelRig(rig);
    sidesSwappedAfterFirstGame(rig);
}

static void test_volleyball_sides_swapped_latch() {
    Rig rig;
    g_fakeMillis = 100000;
    rig.mode.reset(new VolleyballMode(rig.led, rig.back, rig.eink, rig.remote, rig.onModeChange(), rig.users,
                                      std::unique_ptr<Rules>(new ShortVolleyballRules()),
                                      std::function<void(CelebrationVariant)>()));
    sidesSwappedAfterFirstGame(rig);
}

// A game left by UNDO at 0:0 is not the first game: a swap after it is still the start.
static void test_sides_swapped_ignores_abandoned_game() {
    Rig rig;
    squashRig(rig);
    rig.toPlaying();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 0)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x11, rig.describe().screen());

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::SwapSides)));
    TEST_ASSERT_EQUAL_HEX8(0, matchStartFlags(rig));
    TEST_ASSERT_EQUAL_HEX8(0, startToIntro(rig));
    TEST_ASSERT_EQUAL_HEX8(0, skipToPlaying(rig));
    winToMatchStart(rig, 0, 0);

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::SwapSides)));
    TEST_ASSERT_EQUAL_HEX8(SIDES_SWAPPED, matchStartFlags(rig));
    TEST_ASSERT_EQUAL_HEX8(SIDES_SWAPPED, startToIntro(rig));
    TEST_ASSERT_EQUAL_HEX8(SIDES_SWAPPED, skipToPlaying(rig));
}

// Each pair latches at its own first game; CHOOSE_PLAYERS keeps the latch, like the games.
static void test_sides_swapped_per_pair_and_kept() {
    Rig rig;
    squashRig(rig);
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(toggle(UID_CEZ)));
    rig.toMatchStart();   // Anna, Bob, Cez selected

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(pair(UID_ANNA, UID_BOB)));
    startToIntro(rig);
    skipToPlaying(rig);
    winToMatchStart(rig, 0, 0);

    // Cez's first game against Bob with Cez on the right: its own start orientation.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(pair(UID_BOB, UID_CEZ)));
    TEST_ASSERT_EQUAL_HEX8(0, startToIntro(rig));
    TEST_ASSERT_EQUAL_HEX8(0, skipToPlaying(rig));
    winToMatchStart(rig, 0, 0);

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(pair(UID_CEZ, UID_BOB)));
    TEST_ASSERT_EQUAL_HEX8(SIDES_SWAPPED, startToIntro(rig));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Back)));   // INTRO -> MATCH_START
    rig.tick();

    // Back to the first pair: its own latch.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(pair(UID_ANNA, UID_BOB)));
    TEST_ASSERT_EQUAL_HEX8(0, startToIntro(rig));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Back)));
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(pair(UID_BOB, UID_ANNA)));
    TEST_ASSERT_EQUAL_HEX8(SIDES_SWAPPED, startToIntro(rig));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Back)));
    rig.tick();

    // MATCH_START -> CHOOSE_PLAYERS -> MATCH_START keeps the Match and its latch.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Back)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x10, rig.describe().screen());
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartTournament)));
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(pair(UID_BOB, UID_ANNA)));
    TEST_ASSERT_EQUAL_HEX8(SIDES_SWAPPED, startToIntro(rig));
    TEST_ASSERT_EQUAL_HEX8(SIDES_SWAPPED, skipToPlaying(rig));
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

// Points (padel: rallies) for one side, then past the commit window.
static void rallies(Rig &rig, const uint8_t side, const int count) {
    for (int i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, side)));
    }
    rig.commit();
}

// --- LAST_KNOWN / LAST_RIGHT: the newest point still standing (spec 11.3) ------------------

static const uint8_t LAST_KNOWN = 0x10;
static const uint8_t LAST_RIGHT = 0x20;

static uint8_t lastBits(const Rig &rig) {
    return rig.describe().data()[8] & (LAST_KNOWN | LAST_RIGHT);
}

static void test_squash_last_point_flags() {
    Rig rig;
    squashRig(rig);
    rig.toPlaying();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, lastBits(rig), "0:0: unknown");

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 0)));
    TEST_ASSERT_EQUAL_HEX8(LAST_KNOWN, lastBits(rig));
    rig.commit();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 1)));
    TEST_ASSERT_EQUAL_HEX8(LAST_KNOWN | LAST_RIGHT, lastBits(rig));

    // An uncommitted point undone is erased: the left's is the newest again.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 1)));
    TEST_ASSERT_EQUAL_HEX8(LAST_KNOWN, lastBits(rig));

    // L, R committed; undo R marks it lost (until the commit), skipped either way.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 1)));
    rig.commit();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 1)));
    TEST_ASSERT_EQUAL_HEX8(0x01 | LAST_KNOWN, rig.describe().data()[8]);
    rig.commit();
    TEST_ASSERT_EQUAL_HEX8(LAST_KNOWN, lastBits(rig));

    // Down to 0:0 by undoing a committed point: only a lost entry is left.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 0)));
    TEST_ASSERT_EQUAL_HEX8(0x01, rig.describe().data()[8]);
    rig.commit();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, rig.describe().data()[8], "0:0 again: unknown");
    TEST_ASSERT_EQUAL_HEX8(0x13, rig.describe().screen());
}

static void test_padel_last_point_flags() {
    Rig rig;
    padelRig(rig);
    rig.toPlaying();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, lastBits(rig), "set start: unknown");

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 1)));
    TEST_ASSERT_EQUAL_HEX8(LAST_KNOWN | LAST_RIGHT, lastBits(rig));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 1)));
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, lastBits(rig), "back to the set's start");

    // The right takes a gem; in the next gem the left's rally is the newest, undone the gem
    // winner's again (an UNDO there steps back into that gem).
    rallies(rig, 1, 4);
    TEST_ASSERT_EQUAL_HEX8(LAST_KNOWN | LAST_RIGHT, lastBits(rig));
    TEST_ASSERT_EQUAL(1, rig.describe().data()[12]);
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 0)));
    TEST_ASSERT_EQUAL_HEX8(LAST_KNOWN, lastBits(rig));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 0)));
    TEST_ASSERT_EQUAL_HEX8(LAST_KNOWN | LAST_RIGHT, lastBits(rig));

    // A committed rally undone (lost until the commit) is skipped the same way.
    rallies(rig, 0, 1);
    TEST_ASSERT_EQUAL_HEX8(LAST_KNOWN, lastBits(rig));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 0)));
    TEST_ASSERT_EQUAL_HEX8(LAST_KNOWN | LAST_RIGHT, lastBits(rig));
    rig.commit();
    TEST_ASSERT_EQUAL_HEX8(LAST_KNOWN | LAST_RIGHT, lastBits(rig));

    // Stepping back into the gem drops its winning rally: 0:40, the right's third rally newest.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 0)));
    const Body body = rig.describe();
    TEST_ASSERT_EQUAL(0, body.data()[12]);
    TEST_ASSERT_EQUAL(3, body.data()[14]);
    TEST_ASSERT_EQUAL_HEX8(LAST_KNOWN | LAST_RIGHT, lastBits(rig));
}

// --- padel ----------------------------------------------------------------------------

static void test_padel_playing_ladder_and_gem_ball() {
    Rig rig;
    padelRig(rig);
    rig.toPlaying();

    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(cmd(CommandId::Score, 2)));

    // 40:15 committed: gem ball left, points 3 and 1, the newest rally the right's.
    rallies(rig, 0, 3);
    rallies(rig, 1, 1);
    Body body = rig.describe();
    TEST_ASSERT_EQUAL(23, body.size);
    const uint8_t fortyFifteen[15] = {0x01, 0x00, 0x00, 0xA0, 0x02, 0x00, 0x00, 0xB0, 0x34, 0, 0, 0, 0, 3, 1};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fortyFifteen, body.data(), 15);

    // The gem: 1:0 in gems, a fresh 0:0 ladder; the newest point is the gem winner's (left).
    rallies(rig, 0, 1);
    body = rig.describe();
    const uint8_t oneLove[15] = {0x01, 0x00, 0x00, 0xA0, 0x02, 0x00, 0x00, 0xB0, 0x10, 0, 0, 1, 0, 0, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(oneLove, body.data(), 15);

    // UNDO at 0:0 of a new gem steps back into the last one (40:15 again), it does not leave.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 0)));
    body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x13, body.screen());
    TEST_ASSERT_EQUAL(0, body.data()[11]);
    TEST_ASSERT_EQUAL(3, body.data()[13]);
    TEST_ASSERT_EQUAL(1, body.data()[14]);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x34, body.data()[8], "the step-back drops the winning rally: right newest");
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
    TEST_ASSERT_EQUAL_HEX8(0x02 | 0x10, body.data()[8]);   // TIEBREAK, no gem ball at 5:0 of 7, left newest
    TEST_ASSERT_EQUAL(6, body.data()[11]);
    TEST_ASSERT_EQUAL(6, body.data()[12]);
    TEST_ASSERT_EQUAL(5, body.data()[13]);
    TEST_ASSERT_EQUAL(0, body.data()[14]);

    rallies(rig, 0, 1);
    TEST_ASSERT_EQUAL_HEX8(0x02 | 0x04 | 0x10, rig.describe().data()[8]);   // set ball
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
    TEST_ASSERT_EQUAL_HEX8(0x01 | 0x10 | 0x20, body.data()[8]);
    TEST_ASSERT_EQUAL(0, body.data()[11]);
    TEST_ASSERT_EQUAL(1, body.data()[12]);

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 1)));
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, rig.describe().data()[8], "0:0: unknown");
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
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(sport(0x80)));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(cmd(CommandId::Score)));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(cmd(CommandId::Back)));   // a root
    TEST_ASSERT_EQUAL(0, rig.requested.size());

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(sport(0x03)));
    TEST_ASSERT_EQUAL(1, rig.requested.size());
    TEST_ASSERT_EQUAL(DeviceModeState::ShortVolleyballMode, rig.requested[0]);
}

// --- RESULTS (spec 19): what the mode hands GarminService::publishResults --------------

static void test_results_through_the_views() {
    Rig rig;
    squashRig(rig);
    const Tournament *tournament = rig.mode->getTournament();
    TEST_ASSERT_NOT_NULL(tournament);
    TEST_ASSERT_EQUAL(0, tournament->getId());   // drawn at START

    rig.toPlaying();
    TEST_ASSERT_NOT_EQUAL(0, tournament->getId());
    // UNDO at 0:0 abandons the game: nothing finished.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Undo, 0)));
    rig.tick();
    TEST_ASSERT_EQUAL(0, tournament->listedCount());

    startToIntro(rig);
    skipToPlaying(rig);
    winToMatchStart(rig, 1, 0);
    TEST_ASSERT_EQUAL(1, tournament->listedCount());
    uint8_t entry[Garmin::RESULT_ENTRY_SIZE];
    WatchSupport::resultEntry(*tournament, 0, entry);
    const uint8_t want[Garmin::RESULT_ENTRY_SIZE] = {0x01, 0x00, 0x00, 0xA0, 0x02, 0x00, 0x00, 0xB0, 0, 11, 1};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, entry, sizeof(want));

    BatterySensor sensor;
    BatteryMonitor battery(sensor);
    rig.mode.reset(new ModeSwitchingMode(rig.led, rig.back, rig.eink, rig.remote, rig.onModeChange(), battery));
    TEST_ASSERT_NULL(rig.mode->getTournament());
}

// --- ADD / REMOVE_OFFLINE_GAME (spec 10.2 0x0F / 0x10, 16.14): the mode, never the view -----

static WatchCommand addGame(const uint32_t uidA, const uint32_t uidB, const uint8_t a, const uint8_t b) {
    WatchCommand c = cmd(CommandId::AddOfflineGame);
    c.uidA = uidA;
    c.uidB = uidB;
    c.scoreA = a;
    c.scoreB = b;
    return c;
}

static WatchCommand removeGame(const uint32_t tid, const uint16_t rev, const uint16_t index) {
    WatchCommand c = cmd(CommandId::RemoveOfflineGame);
    c.tournamentId = tid;
    c.rev = rev;
    c.index = index;
    return c;
}

// Listed game `index` as RESULTS sends it: A, B, scoreA, scoreB, turn byte.
static void expectListed(const Tournament &tournament, const size_t index, const uint32_t uidA, const uint32_t uidB,
                         const uint8_t a, const uint8_t b, const uint8_t turn) {
    uint8_t entry[Garmin::RESULT_ENTRY_SIZE];
    WatchSupport::resultEntry(tournament, index, entry);
    CHECK(u32(entry) == uidA && u32(entry + 4) == uidB && entry[8] == a && entry[9] == b && entry[10] == turn,
          "listed %u: got %08x-%08x %u:%u turn 0x%02x, expected %08x-%08x %u:%u turn 0x%02x",
          static_cast<unsigned>(index), static_cast<unsigned>(u32(entry)), static_cast<unsigned>(u32(entry + 4)),
          entry[8], entry[9], entry[10], static_cast<unsigned>(uidA), static_cast<unsigned>(uidB), a, b, turn);
}

static void test_offline_games_through_the_modes() {
    Rig rig;
    squashRig(rig);
    const Tournament *tournament = rig.mode->getTournament();
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(toggle(UID_ANNA)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(toggle(UID_BOB)));

    // CHOOSE_PLAYERS: the selection is still open.
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(addGame(UID_ANNA, UID_BOB, 11, 7)));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(removeGame(tournament->getId(), 0, 0)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartTournament)));
    TEST_ASSERT_EQUAL(AckStatus::Busy, rig.send(addGame(UID_ANNA, UID_BOB, 11, 7)));
    rig.tick();
    TEST_ASSERT_EQUAL(0, tournament->listedCount());

    // MATCH_START: stored, the screen untouched (an offline game latches no swap).
    const Body start = rig.describe();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(addGame(UID_ANNA, UID_BOB, 11, 7)));
    TEST_ASSERT_FALSE(rig.mode->viewChangePending());
    const Body startAfter = rig.describe();
    TEST_ASSERT_EQUAL(start.size, startAfter.size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(start.bytes, startAfter.bytes, start.size);
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(addGame(UID_ANNA, UID_BOB, 15, 3)));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(addGame(UID_ANNA, UID_CEZ, 11, 7)));   // not selected
    TEST_ASSERT_EQUAL(1, tournament->listedCount());
    expectListed(*tournament, 0, UID_ANNA, UID_BOB, 11, 7, 0x81);

    // PLAYING: the live game keeps its score, the standing counts the offline games.
    startToIntro(rig);
    skipToPlaying(rig);
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 0)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(addGame(UID_BOB, UID_ANNA, 5, 11)));
    Body body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x13, body.screen());
    TEST_ASSERT_EQUAL(2, body.data()[9]);    // Anna's games
    TEST_ASSERT_EQUAL(0, body.data()[10]);
    TEST_ASSERT_EQUAL(1, body.data()[11]);   // the live 1:0
    TEST_ASSERT_EQUAL(0, body.data()[12]);
    expectListed(*tournament, 1, UID_ANNA, UID_BOB, 11, 5, 0x82);   // the match's own A/B order

    // The live game finishes as the pair's next turn; through CELEBRATION to GAME_OVER.
    for (int i = 0; i < 20 && rig.describe().screen() == 0x13; i++) {
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 0)));
        rig.commit();
        if (rig.mode->viewChangePending()) rig.tick();
    }
    TEST_ASSERT_EQUAL_HEX8(0x14, rig.describe().screen());
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Skip)));
    rig.tick();
    const Body over = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x15, over.screen());
    expectListed(*tournament, 2, UID_ANNA, UID_BOB, 11, 0, 3);

    // GAME_OVER keeps showing its own game after an ADD.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(addGame(UID_ANNA, UID_BOB, 3, 11)));
    body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x15, body.screen());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(over.data(), body.data(), 11);   // uids, winner, 11:0
    TEST_ASSERT_EQUAL(4, tournament->listedCount());
    expectListed(*tournament, 3, UID_ANNA, UID_BOB, 3, 11, 0x84);

    // REMOVE: only an offline game, under the current tournament id and rev (log_epoch:00 first).
    const uint32_t tid = tournament->getId();
    const uint16_t rev = tournament->getRev();
    TEST_ASSERT_EQUAL_HEX16(tournament->status().logEpoch << 8, rev);
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(removeGame(tid, rev + 1, 0)));  // stale rev
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(removeGame(tid ^ 1, rev, 0)));  // other tournament
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(removeGame(tid, rev, 2)));      // board-tracked
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(removeGame(tid, rev, 4)));      // past the end
    TEST_ASSERT_EQUAL_HEX16(rev, tournament->getRev());
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(removeGame(tid, rev, 0)));
    TEST_ASSERT_EQUAL_HEX16(rev + 1, tournament->getRev());
    TEST_ASSERT_EQUAL(3, tournament->listedCount());
    expectListed(*tournament, 0, UID_ANNA, UID_BOB, 11, 5, 0x81);   // later turns renumbered
    expectListed(*tournament, 1, UID_ANNA, UID_BOB, 11, 0, 2);
    expectListed(*tournament, 2, UID_ANNA, UID_BOB, 3, 11, 0x83);
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(removeGame(tid, rev, 0)));      // rev moved on
    body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x15, body.screen());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(over.data(), body.data(), 11);
    TEST_ASSERT_EQUAL(0, rig.requested.size());
}

// The turn byte keeps bit 7 for OFFLINE: a pair's 128th game caps at 127.
static void test_offline_turn_cap() {
    Rig rig;
    squashRig(rig);
    rig.toMatchStart();
    const Tournament *tournament = rig.mode->getTournament();
    for (size_t i = 0; i < Garmin::RESULTS_MAX; i++) {
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(addGame(UID_ANNA, UID_BOB, 11, 2)));
    }
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(addGame(UID_ANNA, UID_BOB, 11, 2)));   // 128 listed
    expectListed(*tournament, 126, UID_ANNA, UID_BOB, 11, 2, 0x80 | 127);
    expectListed(*tournament, 127, UID_ANNA, UID_BOB, 11, 2, 0x80 | 127);
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

    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(focus(0x80)));
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

// --- Tournament sync (spec 20): BEGIN on MENU joins, then grows; ACK, SET_SIDE, leaving ------

static const uint32_t TID = 0x7AB1E701;

// GarminService's roster stage as a peer leaves it.
struct FakeTournamentLink : TournamentLink {
    uint8_t roster[Garmin::TOURNAMENT_ROSTER_MAX * Garmin::ROSTER_ENTRY_SIZE] = {};
    int takes = 0;

    void takeRosterStage(uint8_t count, uint8_t *out) override {
        if (count > Garmin::TOURNAMENT_ROSTER_MAX) count = Garmin::TOURNAMENT_ROSTER_MAX;
        memcpy(out, roster, static_cast<size_t>(count) * Garmin::ROSTER_ENTRY_SIZE);
        memset(roster, 0, sizeof(roster));
        takes++;
    }

    void stageEntry(const uint8_t index, const uint32_t uid, const char *name) {
        uint8_t *e = roster + static_cast<size_t>(index) * Garmin::ROSTER_ENTRY_SIZE;
        Garmin::putU32(e, uid);
        e[4] = 0x10;
        e[5] = static_cast<uint8_t>(0x20 + index);
        e[6] = 0x30;
        memset(e + 7, 0, Garmin::ROSTER_NAME_SIZE);
        memcpy(e + 7, name, strlen(name));
    }
};

static const uint32_t UID_T[] = {0x0000A001, 0x0000B002, 0x0000C003, 0x0000D004};
static const char *const NAME_T[] = {"ALA", "OLA", "EWA", "IGA"};

// Stages entries 0..count-1 and returns their CRC.
static uint32_t stageRoster(FakeTournamentLink &link, const uint8_t count) {
    for (uint8_t i = 0; i < count; i++) link.stageEntry(i, UID_T[i], NAME_T[i]);
    return Garmin::rosterVersion(count, link.roster);
}

static WatchCommand begin(const uint32_t tid, const uint8_t sportId, const uint8_t count, const uint32_t crc) {
    WatchCommand c = cmd(CommandId::BeginTournament);
    c.tournamentId = tid;
    c.sport = sportId;
    c.count = count;
    c.crc = crc;
    return c;
}

static WatchCommand ackChanges(const uint32_t tid, const uint16_t upTo, const uint32_t boundTid,
                               const uint8_t boundEpoch) {
    WatchCommand c = cmd(CommandId::AckChanges);
    c.tournamentId = tid;
    c.upTo = upTo;
    c.boundTid = boundTid;
    c.boundEpoch = boundEpoch;
    return c;
}

AckStatus Rig::ack(const uint32_t tid, const uint16_t upTo) {
    const Tournament *t = mode->getTournament();
    const auto st = t != nullptr ? t->status() : decltype(t->status()){};
    return send(ackChanges(tid, upTo, st.tid, st.logEpoch));
}

static WatchCommand setSide(const uint8_t side, const uint32_t uid) {
    WatchCommand c = cmd(CommandId::SetSide, side);
    c.uid = uid;
    return c;
}

// The active pair's games for roster index `id`, as the views draw them.
static uint8_t tally(const Tournament &tournament, const uint8_t id) {
    return tournament.getActiveMatch()->getMatchResult().scoreOf(id);
}

static uint16_t warnedUnsynced = 0;

static void recordUnsynced(const uint16_t unsynced) {
    warnedUnsynced = unsynced;
}

/** What main.cpp does with a BEGIN on MENU: TournamentSync::checkJoin, then the sport's mode. */
struct SyncRig : Rig {
    FakeTournamentLink link;
    TournamentContext context{link, recordUnsynced};
    TournamentJoin join = {};

    void build(const DeviceModeState state, const TournamentJoin *joining) {
        g_fakeMillis = 100000;
        mode.reset();
        const std::function<void(CelebrationVariant)> noSound;
        switch (state) {
            case DeviceModeState::PadelMode:
                mode.reset(new PadelMode(led, back, eink, remote, onModeChange(), users, noSound, &context, joining));
                break;
            case DeviceModeState::VolleyballMode:
                mode.reset(new VolleyballMode(led, back, eink, remote, onModeChange(), users,
                                              std::unique_ptr<Rules>(new VolleyballRules()), noSound, &context,
                                              joining));
                break;
            default:
                mode.reset(new SquashMode(led, back, eink, remote, onModeChange(), users, noSound, &context, joining));
                break;
        }
        tick();
    }

    // MENU's BEGIN with `count` staged entries; APPLIED builds the sport's mode on the join.
    AckStatus joinFromMenu(const WatchCommand &c, const DeviceModeState expected = DeviceModeState::SquashMode) {
        DeviceModeState state = DeviceModeState::ModeSwitchingMode;
        const AckStatus status = TournamentSync::checkJoin(link, c, join, state);
        if (status == AckStatus::Applied) {
            TEST_ASSERT_EQUAL(expected, state);
            build(state, &join);
        }
        return status;
    }

    void joinSport(const uint8_t sportId, const DeviceModeState expected, const uint8_t count = 3) {
        const uint32_t crc = stageRoster(link, count);
        TEST_ASSERT_EQUAL(AckStatus::Applied, joinFromMenu(::begin(TID, sportId, count, crc), expected));
        TEST_ASSERT_EQUAL_HEX8(0x11, describe().screen());
    }
};

static bool stageEmpty(const FakeTournamentLink &link) {
    for (const uint8_t b : link.roster) {
        if (b != 0) return false;
    }
    return true;
}

// Every refusal clears the stage, so each one re-stages.
static void test_tournament_join_refusals() {
    SyncRig rig;
    const uint32_t crc = stageRoster(rig.link, 2);
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.joinFromMenu(begin(TID, 0x01, 2, crc ^ 1)));   // CRC
    TEST_ASSERT_TRUE(stageEmpty(rig.link));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.joinFromMenu(begin(TID, 0x01, 2, crc)));       // stage cleared
    TEST_ASSERT_TRUE(stageEmpty(rig.link));
    stageRoster(rig.link, 2);
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.joinFromMenu(begin(0, 0x01, 2, crc)));         // tid 0
    TEST_ASSERT_TRUE(stageEmpty(rig.link));
    stageRoster(rig.link, 2);
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.joinFromMenu(begin(TID, 0x09, 2, crc)));       // no such sport
    TEST_ASSERT_TRUE(stageEmpty(rig.link));
    stageRoster(rig.link, 2);
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.joinFromMenu(begin(TID, 0x00, 2, crc)));       // sport none
    TEST_ASSERT_TRUE(stageEmpty(rig.link));
    // 17 entries: the stage holds 16, so the count alone is refused.
    for (uint8_t i = 0; i < Garmin::TOURNAMENT_ROSTER_MAX; i++) {
        rig.link.stageEntry(i, 0x00010000u + i, "P");
    }
    const uint32_t crc16 = Garmin::rosterVersion(16, rig.link.roster);
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.joinFromMenu(begin(TID, 0x01, 17, crc16)));
    TEST_ASSERT_TRUE(stageEmpty(rig.link));
    TEST_ASSERT_EQUAL(6, rig.link.takes);
    TEST_ASSERT_EQUAL(0, rig.requested.size());
    TEST_ASSERT_TRUE(rig.mode == nullptr);
}

// Without a context (the host's plain rigs) BEGIN is WRONG_SCREEN; the board's own tournament
// takes ACK_CHANGES and SET_SIDE like a joined one.
static void test_tournament_commands_on_a_local_tournament() {
    Rig rig;
    squashRig(rig);
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(begin(TID, 0x01, 2, 0)));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(setSide(0, UID_ANNA)));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.ack(TID, 0));   // no tournament yet

    rig.toMatchStart();
    const Tournament *tournament = rig.mode->getTournament();
    TEST_ASSERT_TRUE(tournament->running());
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(begin(tournament->getId(), 0x01, 2, 0)));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.ack(tournament->getId() ^ 1, 0));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.ack(tournament->getId(), 0));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(setSide(0, UID_CEZ)));   // not in the roster
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(setSide(0, UID_ANNA)));  // already there
}

// The picker only grows a running tournament: a roster entry cannot be un-selected, and MATCH_START's
// BACK returns to CHOOSE_PLAYERS with the tournament kept.
static void test_picker_grows_only() {
    Rig rig;
    squashRig(rig);
    rig.toMatchStart();
    const Tournament *tournament = rig.mode->getTournament();
    const uint32_t tid = tournament->getId();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Back)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x10, rig.describe().screen());
    TEST_ASSERT_EQUAL(0, rig.requested.size());
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(toggle(UID_ANNA)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(toggle(UID_CEZ)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(toggle(UID_CEZ)));   // not started yet: out again
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(toggle(UID_CEZ)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartTournament)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x11, rig.describe().screen());
    TEST_ASSERT_EQUAL_HEX32(tid, tournament->getId());
    TEST_ASSERT_EQUAL(3, tournament->status().rosterCount);
    TEST_ASSERT_EQUAL_HEX32(0x07, u32(rig.describe().data()));
}

// BEGIN on MENU -> MATCH_START -> a played game -> change log -> ACK -> RESULTS, then growth.
static void test_tournament_join_game_ack() {
    SyncRig rig;
    const uint32_t crc = stageRoster(rig.link, 3);
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.joinFromMenu(begin(TID, 0x01, 3, crc)));
    const Tournament *tournament = rig.mode->getTournament();
    TEST_ASSERT_TRUE(tournament->running());
    TEST_ASSERT_EQUAL_HEX32(TID, tournament->getId());
    TEST_ASSERT_EQUAL_HEX32(crc, tournament->status().rosterCrc);
    // Every entry in, no CHOOSE_PLAYERS: MATCH_START with the first two.
    Body body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x11, body.screen());
    TEST_ASSERT_EQUAL_HEX32(0x07, u32(body.data()));
    TEST_ASSERT_EQUAL_HEX32(UID_T[0], u32(body.data() + 4));
    TEST_ASSERT_EQUAL_HEX32(UID_T[1], u32(body.data() + 8));
    TEST_ASSERT_TRUE(rig.mode->isInMatch());

    // A tracked game: a board record at seq 1, listed and counted.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartMatch)));
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Skip)));
    rig.tick();
    for (int i = 0; i < 20 && rig.describe().screen() == 0x13; i++) {
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 0)));
        rig.commit();
        if (rig.mode->viewChangePending()) rig.tick();
    }
    TEST_ASSERT_EQUAL_HEX8(0x14, rig.describe().screen());
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Skip)));
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::NextGame)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x11, rig.describe().screen());
    TEST_ASSERT_EQUAL(1, tournament->listedCount());
    TEST_ASSERT_EQUAL(1, tally(*tournament, 0));
    const TournamentLog::Entry &played = tournament->getLog().stored(0);
    TEST_ASSERT_EQUAL(1, played.seq);
    TEST_ASSERT_EQUAL(TournamentLog::BOARD, played.writer);
    TEST_ASSERT_NOT_EQUAL(0, played.record.id);
    TEST_ASSERT_EQUAL_HEX32(UID_T[0], played.record.uidA);
    TEST_ASSERT_EQUAL_HEX32(UID_T[1], played.record.uidB);
    TEST_ASSERT_EQUAL(11, played.record.scoreA);
    TEST_ASSERT_EQUAL(0, played.record.scoreB);
    expectListed(*tournament, 0, UID_T[0], UID_T[1], 11, 0, 1);

    const auto status = tournament->status();
    TEST_ASSERT_EQUAL_HEX32(TID, status.tid);
    TEST_ASSERT_EQUAL(1, status.changesTo);
    TEST_ASSERT_EQUAL(1, status.gamesCount);

    // ACK: another tid or past changes_to is INVALID; then nothing is unsynced, games stay.
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.ack(TID ^ 1, 1));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.ack(TID, 2));
    // Bound to another log: a STATUS read before a leave and re-join, or none at all.
    const uint8_t epoch = rig.mode->getTournament()->status().logEpoch;
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(ackChanges(TID, 1, TID, epoch ^ 1)));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(ackChanges(TID, 1, 0, 0)));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.ack(TID, 1));
    TEST_ASSERT_FALSE(tournament->unsynced());
    TEST_ASSERT_EQUAL(1, tally(*tournament, 0));

    // Growth: same tid, a renamed entry and a late joiner appended.
    stageRoster(rig.link, 3);
    rig.link.stageEntry(0, UID_T[0], "ALICJA");
    rig.link.stageEntry(3, UID_T[3], NAME_T[3]);
    const uint32_t crc4 = Garmin::rosterVersion(4, rig.link.roster);
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(begin(TID, 0x01, 4, crc4)));
    TEST_ASSERT_EQUAL(4, tournament->status().rosterCount);
    TEST_ASSERT_EQUAL_HEX32(crc4, tournament->status().rosterCrc);
    TEST_ASSERT_EQUAL_STRING("ALICJA", tournament->getRoster().profiles()[0]->getName());
    TEST_ASSERT_EQUAL(1, tournament->listedCount());   // games untouched
    // The joiner is in: a watch's offline game with them is a board record at seq 2.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(addGame(UID_T[3], UID_T[0], 11, 5)));
    TEST_ASSERT_EQUAL(2, tournament->listedCount());
    TEST_ASSERT_EQUAL(1, tournament->unsyncedCount());
    TEST_ASSERT_TRUE(tournament->listedRecord(1).offline());
    // Another tid, another sport, a dropped entry: INVALID, nothing changes.
    stageRoster(rig.link, 4);
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(begin(TID ^ 1, 0x01, 4, Garmin::rosterVersion(4, rig.link.roster))));
    stageRoster(rig.link, 4);
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(begin(TID, 0x04, 4, Garmin::rosterVersion(4, rig.link.roster))));
    stageRoster(rig.link, 2);
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(begin(TID, 0x01, 2, Garmin::rosterVersion(2, rig.link.roster))));
    TEST_ASSERT_EQUAL(4, tournament->status().rosterCount);
    TEST_ASSERT_EQUAL_HEX32(crc4, tournament->status().rosterCrc);
    // SET_SIDE on MATCH_START: the left side becomes EWA, OLA stays right.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(setSide(0, UID_T[2])));
    body = rig.describe();
    TEST_ASSERT_EQUAL_HEX32(UID_T[2], u32(body.data() + 4));
    TEST_ASSERT_EQUAL_HEX32(UID_T[1], u32(body.data() + 8));

    // Long C on MATCH_START steps back to the picker; the tournament stays.
    TEST_ASSERT_TRUE(rig.mode->goBack());
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x10, rig.describe().screen());
    TEST_ASSERT_EQUAL(0, rig.requested.size());
    TEST_ASSERT_EQUAL_HEX32(TID, tournament->getId());
    // Not a match screen: BEGIN and SET_SIDE wait; ACK_CHANGES is taken.
    stageRoster(rig.link, 4);
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(begin(TID, 0x01, 4, crc4)));
    TEST_ASSERT_TRUE(stageEmpty(rig.link));
    TEST_ASSERT_EQUAL(AckStatus::WrongScreen, rig.send(setSide(0, UID_T[0])));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.ack(TID, 2));
}

// PLAYING's score bytes (body 11 / 12: squash and volleyball points, padel gems).
static void expectPlaying(SyncRig &rig, const uint32_t left, const uint32_t right, const uint8_t leftScore,
                          const uint8_t rightScore) {
    const Body body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x13, body.screen());
    TEST_ASSERT_EQUAL_HEX32(left, u32(body.data()));
    TEST_ASSERT_EQUAL_HEX32(right, u32(body.data() + 4));
    TEST_ASSERT_EQUAL(leftScore, body.data()[11]);
    TEST_ASSERT_EQUAL(rightScore, body.data()[12]);
}

/**
 * SET_SIDE mid-PLAYING: the running game moves to ALA-EWA with its score, OLA's match keeps
 * nothing, and finishing it goes through the view's refreshed match (a stale one has no Game,
 * so its finishGame() would dereference null). On CELEBRATION it sets the next pair only.
 */
static void setSideMidGame(const uint8_t sportId, const DeviceModeState state, const uint8_t leftScore,
                           const uint8_t rightScore) {
    SyncRig rig;
    rig.joinSport(sportId, state);
    const Tournament *tournament = rig.mode->getTournament();

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartMatch)));
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Skip)));
    rig.tick();
    Match *before = tournament->getActiveMatch();
    expectPlaying(rig, UID_T[0], UID_T[1], 0, 0);

    // Squash / volleyball: 3:2 in points. Padel: one gem to the left, then 0:30 in the second.
    if (state == DeviceModeState::PadelMode) {
        rallies(rig, 0, 4);
        rallies(rig, 1, 2);
    } else {
        rallies(rig, 0, 3);
        rallies(rig, 1, 2);
    }
    expectPlaying(rig, UID_T[0], UID_T[1], leftScore, rightScore);

    // Refusals change nothing: no such side, an unknown uid, the kept (left) player.
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(setSide(2, UID_T[2])));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(setSide(1, UID_NOBODY)));
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(setSide(1, UID_T[0])));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(setSide(1, UID_T[1])));   // already there
    TEST_ASSERT_EQUAL_PTR(before, tournament->getActiveMatch());

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(setSide(1, UID_T[2])));
    Match *after = tournament->getActiveMatch();
    TEST_ASSERT_TRUE(after != before);
    TEST_ASSERT_FALSE(before->hasGame());
    TEST_ASSERT_TRUE(after->hasGame());
    expectPlaying(rig, UID_T[0], UID_T[2], leftScore, rightScore);
    if (state == DeviceModeState::PadelMode) {
        TEST_ASSERT_EQUAL(0, rig.describe().data()[13]);   // the gem ladder kept: Love
        TEST_ASSERT_EQUAL(2, rig.describe().data()[14]);   // Thirty
    }
    rig.tick();   // a frame on the refreshed view

    for (int i = 0; i < 300 && rig.describe().screen() == 0x13; i++) {
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 0)));
        rig.commit();
        if (rig.mode->viewChangePending()) rig.tick();
    }
    TEST_ASSERT_EQUAL_HEX8(0x14, rig.describe().screen());
    TEST_ASSERT_EQUAL(0, before->getMatchResult().scoreOf(before->getPlayerA().getId())
                         + before->getMatchResult().scoreOf(before->getPlayerB().getId()));
    TEST_ASSERT_EQUAL(1, tournament->listedCount());
    const GameRecord played = tournament->listedRecord(0);
    TEST_ASSERT_EQUAL_HEX32(UID_T[0], played.uidA);
    TEST_ASSERT_EQUAL_HEX32(UID_T[2], played.uidB);
    TEST_ASSERT_EQUAL(rightScore, played.scoreB);   // the right side's score survived the swap
    TEST_ASSERT_TRUE(played.scoreA > played.scoreB);

    // CELEBRATION: OLA takes the left side for the next game; the screen keeps the finished one.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(setSide(0, UID_T[1])));
    Body body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x14, body.screen());
    TEST_ASSERT_EQUAL_HEX32(UID_T[0], u32(body.data()));
    TEST_ASSERT_EQUAL_HEX32(UID_T[2], u32(body.data() + 4));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Skip)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x15, rig.describe().screen());
    TEST_ASSERT_EQUAL_HEX32(UID_T[0], u32(rig.describe().data()));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::NextGame)));
    rig.tick();
    body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x11, body.screen());
    TEST_ASSERT_EQUAL_HEX32(UID_T[1], u32(body.data() + 4));
    TEST_ASSERT_EQUAL_HEX32(UID_T[2], u32(body.data() + 8));
    TEST_ASSERT_EQUAL(1, tournament->listedCount());

    // INTRO moves the pair as well.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartMatch)));
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(setSide(1, UID_T[0])));
    body = rig.describe();
    TEST_ASSERT_EQUAL_HEX8(0x12, body.screen());
    TEST_ASSERT_EQUAL_HEX32(UID_T[1], u32(body.data()));
    TEST_ASSERT_EQUAL_HEX32(UID_T[0], u32(body.data() + 4));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x12, rig.describe().screen());   // the wipe replays, no hand-over
}

static void test_tournament_set_side_squash() {
    setSideMidGame(0x01, DeviceModeState::SquashMode, 3, 2);
}

static void test_tournament_set_side_volleyball() {
    setSideMidGame(0x02, DeviceModeState::VolleyballMode, 3, 2);
}

static void test_tournament_set_side_padel() {
    setSideMidGame(0x04, DeviceModeState::PadelMode, 1, 0);
}

// START_MATCH keeps a live slot until the game's record exists; INTRO's BACK gives it back, and at
// the live cap START_MATCH is refused.
static void test_start_match_slot_and_cap() {
    SyncRig rig;
    rig.joinSport(0x01, DeviceModeState::SquashMode);
    const Tournament *tournament = rig.mode->getTournament();
    for (uint16_t i = 0; i < Garmin::RESULTS_MAX - 1; i++) {
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(addGame(UID_T[i % 3], UID_T[(i + 1) % 3], 11, 4)));
    }
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartMatch)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x12, rig.describe().screen());
    TEST_ASSERT_TRUE(tournament->matchSlotReserved());
    // The last slot is the coming game's.
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(addGame(UID_T[0], UID_T[2], 11, 4)));

    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Back)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x11, rig.describe().screen());
    TEST_ASSERT_FALSE(tournament->matchSlotReserved());

    // The reserved slot takes the game to its record.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartMatch)));
    rig.tick();
    TEST_ASSERT_TRUE(tournament->matchSlotReserved());
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Skip)));
    rig.tick();
    for (int i = 0; i < 20 && rig.describe().screen() == 0x13; i++) {
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 0)));
        rig.commit();
        if (rig.mode->viewChangePending()) rig.tick();
    }
    TEST_ASSERT_EQUAL_HEX8(0x14, rig.describe().screen());
    TEST_ASSERT_EQUAL(Garmin::RESULTS_MAX, tournament->listedCount());
    TEST_ASSERT_FALSE(tournament->matchSlotReserved());
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Skip)));
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::NextGame)));
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(addGame(UID_T[0], UID_T[2], 11, 4)));

    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(cmd(CommandId::StartMatch)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x11, rig.describe().screen());
    TEST_ASSERT_FALSE(tournament->matchSlotReserved());
}

// Leaving from CHOOSE_PLAYERS (20.6): a watch's BACK is refused and the fob's long C warns while
// changes are unacknowledged; once ACKed, either leaves for the menu.
static void test_tournament_leave_with_unsynced_changes() {
    warnedUnsynced = 0;
    SyncRig rig;
    rig.joinSport(0x01, DeviceModeState::SquashMode);

    // The guard arms on the first ACK.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.ack(TID, 0));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(addGame(UID_T[0], UID_T[2], 11, 4)));
    TEST_ASSERT_EQUAL(1, rig.mode->getTournament()->unsyncedCount());

    // BACK steps back on the match screens: MATCH_START -> CHOOSE_PLAYERS.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Back)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x10, rig.describe().screen());

    TEST_ASSERT_EQUAL(AckStatus::Invalid, rig.send(cmd(CommandId::Back)));
    TEST_ASSERT_TRUE(rig.mode->goBack());
    TEST_ASSERT_EQUAL(1, warnedUnsynced);
    TEST_ASSERT_EQUAL(0, rig.requested.size());

    // ACKed: the watch's BACK leaves at once.
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.ack(TID, rig.mode->getTournament()->status().changesTo));
    warnedUnsynced = 0;
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Back)));
    TEST_ASSERT_EQUAL(0, warnedUnsynced);
    TEST_ASSERT_EQUAL(1, rig.requested.size());
    TEST_ASSERT_EQUAL(DeviceModeState::ModeSwitchingMode, rig.requested[0]);
    TEST_ASSERT_TRUE(rig.mode->goBack());
    TEST_ASSERT_EQUAL(2, rig.requested.size());
    TEST_ASSERT_EQUAL(0, warnedUnsynced);
}

// A log nobody ever ACKed is not armed: both ways out leave at once (20.6).
static void test_tournament_leave_unarmed() {
    warnedUnsynced = 0;
    SyncRig rig;
    rig.joinSport(0x01, DeviceModeState::SquashMode);
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(addGame(UID_T[0], UID_T[2], 11, 4)));
    TEST_ASSERT_FALSE(rig.mode->getTournament()->unsynced());
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Back)));
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x10, rig.describe().screen());
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Back)));
    TEST_ASSERT_EQUAL(1, rig.requested.size());
    TEST_ASSERT_TRUE(rig.mode->goBack());
    TEST_ASSERT_EQUAL(2, rig.requested.size());
    TEST_ASSERT_EQUAL(0, warnedUnsynced);
}

// Growth on PLAYING and GAME_OVER: the running game, its match and the screen stay.
static void test_tournament_growth_mid_game() {
    SyncRig rig;
    rig.joinSport(0x01, DeviceModeState::SquashMode);
    const Tournament *tournament = rig.mode->getTournament();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::StartMatch)));
    rig.tick();
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Skip)));
    rig.tick();
    rallies(rig, 0, 3);
    Match *active = tournament->getActiveMatch();

    const uint32_t crc4 = stageRoster(rig.link, 4);
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(begin(TID, 0x01, 4, crc4)));
    TEST_ASSERT_EQUAL(4, tournament->status().rosterCount);
    TEST_ASSERT_EQUAL_PTR(active, tournament->getActiveMatch());
    TEST_ASSERT_TRUE(active->hasGame());
    expectPlaying(rig, UID_T[0], UID_T[1], 3, 0);
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.ack(TID, 0));
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(setSide(1, UID_T[1])));
    rig.tick();

    for (int i = 0; i < 20 && rig.describe().screen() == 0x13; i++) {
        TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(cmd(CommandId::Score, 0)));
        rig.commit();
        if (rig.mode->viewChangePending()) rig.tick();
    }
    TEST_ASSERT_EQUAL_HEX8(0x14, rig.describe().screen());
    TEST_ASSERT_EQUAL(1, tournament->listedCount());
    stageRoster(rig.link, 4);
    rig.link.stageEntry(4, 0x0000E005, "ULA");
    const uint32_t crc5 = Garmin::rosterVersion(5, rig.link.roster);
    TEST_ASSERT_EQUAL(AckStatus::Applied, rig.send(begin(TID, 0x01, 5, crc5)));
    TEST_ASSERT_EQUAL(5, tournament->status().rosterCount);
    TEST_ASSERT_EQUAL_HEX8(0x14, rig.describe().screen());
    TEST_ASSERT_EQUAL(1, tournament->listedCount());
    rig.tick();
    TEST_ASSERT_EQUAL_HEX8(0x14, rig.describe().screen());
}

static int overlayConfirms = 0;

static void countConfirm() {
    overlayConfirms++;
}

// The long-C confirm: only while showing and only when the content asks for it.
static void test_overlay_long_c_confirm() {
    overlayConfirms = 0;
    Overlay overlay;
    const OverlayContent plain = {"T", "L", LedText::toWord("HoLd"), Colors::Yellow, 4000, nullptr};
    overlay.show(plain, 1000);
    TEST_ASSERT_TRUE(overlay.active(1000));
    TEST_ASSERT_FALSE(overlay.confirmsLongC());
    overlay.confirmLongC();
    TEST_ASSERT_TRUE(overlay.active(1050));

    const OverlayContent confirm = {"T", "L", LedText::toWord("HoLd"), Colors::Yellow, 4000, countConfirm};
    overlay.show(confirm, 2000);
    TEST_ASSERT_TRUE(overlay.confirmsLongC());
    overlay.confirmLongC();
    TEST_ASSERT_EQUAL(1, overlayConfirms);
    TEST_ASSERT_FALSE(overlay.active(2050));
    TEST_ASSERT_TRUE(overlay.takeFinished());
    overlay.confirmLongC();
    TEST_ASSERT_EQUAL(1, overlayConfirms);

    // Timed out: no confirm, and a later plain show() drops the callback.
    overlay.show(confirm, 10000);
    TEST_ASSERT_FALSE(overlay.active(14000));
    TEST_ASSERT_FALSE(overlay.confirmsLongC());
    overlay.show(plain, 20000);
    TEST_ASSERT_FALSE(overlay.confirmsLongC());
    TEST_ASSERT_EQUAL(1, overlayConfirms);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_route_pending_is_busy_even_for_back);
    RUN_TEST(test_squash_choose_players);
    RUN_TEST(test_selection_restored_on_mode_entry);
    RUN_TEST(test_squash_match_start);
    RUN_TEST(test_squash_playing_score_undo_and_game_ball);
    RUN_TEST(test_squash_winning_commit_through_loop);
    RUN_TEST(test_padel_set_win_through_loop);
    RUN_TEST(test_celebration_and_game_over_describe);
    RUN_TEST(test_squash_court_side_after_swap);
    RUN_TEST(test_squash_sides_swapped_latch);
    RUN_TEST(test_padel_sides_swapped_latch);
    RUN_TEST(test_volleyball_sides_swapped_latch);
    RUN_TEST(test_sides_swapped_ignores_abandoned_game);
    RUN_TEST(test_sides_swapped_per_pair_and_kept);
    RUN_TEST(test_squash_undo_at_zero_leaves_for_match_start);
    RUN_TEST(test_squash_last_point_flags);
    RUN_TEST(test_padel_last_point_flags);
    RUN_TEST(test_padel_playing_ladder_and_gem_ball);
    RUN_TEST(test_padel_tiebreak_flag_and_raw_points);
    RUN_TEST(test_padel_undo_with_nothing_to_step_back_leaves);
    RUN_TEST(test_volleyball_scores_through_the_same_path);
    RUN_TEST(test_menu_select_sport_and_describe);
    RUN_TEST(test_results_through_the_views);
    RUN_TEST(test_offline_games_through_the_modes);
    RUN_TEST(test_offline_turn_cap);
    RUN_TEST(test_menu_focus);
    RUN_TEST(test_choose_players_focus);
    RUN_TEST(test_choose_players_focus_padel_and_volleyball);
    RUN_TEST(test_focus_wrong_screen_while_playing);
    RUN_TEST(test_tournament_join_refusals);
    RUN_TEST(test_tournament_commands_on_a_local_tournament);
    RUN_TEST(test_picker_grows_only);
    RUN_TEST(test_tournament_join_game_ack);
    RUN_TEST(test_tournament_set_side_squash);
    RUN_TEST(test_tournament_set_side_volleyball);
    RUN_TEST(test_tournament_set_side_padel);
    RUN_TEST(test_start_match_slot_and_cap);
    RUN_TEST(test_tournament_leave_with_unsynced_changes);
    RUN_TEST(test_tournament_leave_unarmed);
    RUN_TEST(test_tournament_growth_mid_game);
    RUN_TEST(test_overlay_long_c_confirm);
    return UNITY_END();
}
