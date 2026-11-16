#ifndef VOLLEYBALL_MODE_H
#define VOLLEYBALL_MODE_H

#include "VolleyballModeState.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/TournamentSync.h"
#include "DeviceMode/View.h"
#include "Tournament/Tournament.h"
#include "Tournament/SelectionStore.h"
#include "Tournament/Rules/Rules.h"
#include "RemoteDevelopmentService/LoggerHelper.h"
#include "Views/VolleyballMatchStartGameView.h"
#include "Views/VolleyballGameOverView.h"
#include "DeviceMode/Celebration/GameCelebrationView.h"
#include "DeviceMode/Intro/MatchIntroView.h"
#include "Views/VolleyballGamePlayingView.h"
#include "Views/VolleyballTournamentChoosePlayersView.h"
#include "Utils.h"

enum class VolleyballModeState : uint8_t;

class VolleyballMode final : public DeviceMode {
    VolleyballModeState state = VolleyballModeState::Init;
    VolleyballModeState previousState = VolleyballModeState::Init;
    Tournament tournament;
    std::vector<UserProfile *> &users;
    std::function<void(CelebrationVariant)> onMatchOver;
    // Spec 20's roster stage and the unsynced warning; null in the host tests.
    const TournamentContext *sync;
    // Volleyball or short volleyball: STATUS's sport for a tournament started here.
    uint8_t sport;
    // GAME_OVER's game: SET_SIDE on CELEBRATION moves the active match to the next pair.
    const Match *celebratedMatch = nullptr;

    bool onTournamentScreen() const {
        return state == VolleyballModeState::MatchStartGame || state == VolleyballModeState::MatchIntro
               || state == VolleyballModeState::GamePlaying
               || state == VolleyballModeState::GameCelebration || state == VolleyballModeState::GameOver;
    }

    // Fob C on CHOOSE_PLAYERS: to MENU, which drops the tournament (spec 20.6) - after a
    // warning while it is unsynced.
    void leave() {
        TournamentSync::leave(sync, tournament, onDeviceModeChange);
    }

    void setState(const VolleyballModeState newState) {
        state = newState;
    }

    void handleStateChange() {
        // START (fob or watch) is the only way out of the choose list forward.
        if (previousState == VolleyballModeState::TournamentChoosePlayers && state == VolleyballModeState::MatchStartGame) {
            // A joined roster is the peer's: the board's own rematch selection stays.
            if (!tournament.isJoined()) SelectionStore::save(SelectionStore::Sport::Volleyball, tournament.getPlayers());
            tournament.start(sport);
        }
        previousState = state;
        // Timer-driven hand-overs (celebration, intro) call no preventTriggerForMs():
        // a press latched in between must not act on the new view's first frame.
        // Only a fob skip of the intro passes its press on.
        remoteInputManager.clearLatchesForNewView();

        switch (state) {
            case VolleyballModeState::TournamentChoosePlayers:
                activeView = std::make_unique<VolleyballTournamentChoosePlayersView>(
                    tournament,
                    users,
                    [this](DeviceModeState) { leave(); },
                    [this](const VolleyballModeState newState) { setState(newState); }
                );
                break;
            case VolleyballModeState::MatchStartGame:
                activeView = std::make_unique<VolleyballMatchStartGameView>(
                    tournament,
                    onDeviceModeChange,
                    [this](const VolleyballModeState newState) { setState(newState); }
                );
                break;
            case VolleyballModeState::MatchIntro:
                activeView = std::make_unique<MatchIntroView<VolleyballModeState>>(
                    tournament,
                    [this](const VolleyballModeState newState) { setState(newState); },
                    Str::MATCH_SCORE_LABEL_SETS,
                    true
                );
                break;
            case VolleyballModeState::GamePlaying:
                activeView = std::make_unique<VolleyballGamePlayingView>(
                    tournament,
                    [this](const VolleyballModeState newState) { setState(newState); }
                );
                break;
            case VolleyballModeState::GameCelebration: {
                auto view = std::make_unique<GameCelebrationView<VolleyballModeState>>(
                    tournament,
                    [this](const VolleyballModeState newState) { setState(newState); },
                    Str::MATCH_SCORE_LABEL_SETS,
                    false
                );
                celebratedMatch = tournament.getActiveMatch();
                // Once per game: restoreView() after an Overlay never comes through here.
                if (onMatchOver) {
                    onMatchOver(view->getVariant());
                }

                activeView = std::move(view);
                break;
            }
            case VolleyballModeState::GameOver:
                activeView = std::make_unique<VolleyballGameOverView>(
                    celebratedMatch,
                    [this](const VolleyballModeState newState) { setState(newState); }
                );
                break;
            default:
                printLn("TRIED TO CHANGE TO UNSUPPORTED STATE");
                break;
        }

        activeView->initLedDisplay(ledDisplay);
        activeView->initBackDisplay(backDisplay);
        activeView->initEInkDisplay(einkDisplay);
    }

public:
    VolleyballMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        EInkDisplay &einkDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::vector<UserProfile *> &users,
        std::unique_ptr<Rules> rules,
        std::function<void(CelebrationVariant)> onMatchOver,
        const TournamentContext *sync = nullptr,
        const TournamentJoin *join = nullptr,
        const Garmin::SportId sportId = Garmin::SportId::Volleyball
    )
        : DeviceMode(ledDisplay, backDisplay, einkDisplay, remoteInputManager, onDeviceModeChange),
          tournament(std::move(rules)), users(users), onMatchOver(std::move(onMatchOver)), sync(sync),
          sport(static_cast<uint8_t>(sportId)) {

        ledDisplay.setSameSideMode(true);
        backDisplay.setSameSideMode(true);
        if (join != nullptr
            && tournament.join(join->tournamentId, join->sport, join->count, join->rosterCrc, join->entries)) {
            // A peer's tournament (spec 20.3): every entry selected, straight to MATCH_START.
            setState(VolleyballModeState::MatchStartGame);
            return;
        }
        SelectionStore::restore(SelectionStore::Sport::Volleyball, tournament, users);
        setState(VolleyballModeState::TournamentChoosePlayers);
    }

    const Tournament *getTournament() const override {
        return &tournament;
    }

    // Not on CHOOSE_PLAYERS: the selection, and so the players an ADD may name, is still open.
    Garmin::AckStatus editOfflineGames(const WatchCommand &command) override {
        return WatchSupport::editOfflineGames(onTournamentScreen() ? &tournament : nullptr, command);
    }

    Tournament *editTournament() override {
        return &tournament;
    }

    Garmin::AckStatus handleTournamentCommand(const WatchCommand &command) override {
        return TournamentSync::handle(sync, tournament, command, onTournamentScreen());
    }

    bool isInMatch() const override {
        return state != VolleyballModeState::TournamentChoosePlayers;
    }

    // setState() swaps the view at the next loop(); until then a watch command gets BUSY.
    bool viewChangePending() const override {
        return state != previousState;
    }

    // Spec 20.6: a peer's BACK does not leave a tournament with unsynced changes.
    Garmin::AckStatus goBackFromWatch() override {
        if (state == VolleyballModeState::TournamentChoosePlayers) {
            return TournamentSync::leaveFromWatch(tournament, onDeviceModeChange);
        }
        return DeviceMode::goBackFromWatch();
    }

    bool goBack() override {
        switch (state) {
            case VolleyballModeState::TournamentChoosePlayers:
                leave();
                return true;
            case VolleyballModeState::MatchStartGame:
                // The tournament stays; START again appends the newly selected (spec 20.3).
                setState(VolleyballModeState::TournamentChoosePlayers);
                return true;
            case VolleyballModeState::MatchIntro:
                // No game exists yet (GamePlaying creates it), so nothing is lost.
                tournament.releaseMatchSlot();
                setState(VolleyballModeState::MatchStartGame);
                return true;
            case VolleyballModeState::GameCelebration:
            case VolleyballModeState::GameOver:
                // The result is already recorded; back cannot undo it, so it lands where
                // the forward action would.
                setState(VolleyballModeState::MatchStartGame);
                return true;
            case VolleyballModeState::GamePlaying:
                // Deliberate, not a missing case: a hold must never discard a live game.
                return false;
            default:
                return false;
        }
    }

    void loop() override {
        if (state != previousState) {
            handleStateChange();
        }

        if (activeView) {
            activeView->handleInput(remoteInputManager);
            // A swap the input requested lands next loop, and the outgoing view must not draw
            // again: a winning commit's finishGame() has freed the Game it points at.
            if (state != previousState) {
                return;
            }
            activeView->renderLedDisplay(ledDisplay);
            activeView->renderBackDisplay(backDisplay);
            activeView->renderEInkDisplay(einkDisplay);
        }
    }
};

#endif //VOLLEYBALL_MODE_H
