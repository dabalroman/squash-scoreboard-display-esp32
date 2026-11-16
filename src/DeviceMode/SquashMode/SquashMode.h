#ifndef SQUASH_MODE_H
#define SQUASH_MODE_H

#include <memory>
#include "SquashModeState.h"
#include "Utils.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/TournamentSync.h"
#include "DeviceMode/View.h"
#include "Tournament/Tournament.h"
#include "Tournament/SelectionStore.h"
#include "Tournament/Rules/SquashRules.h"
#include "RemoteDevelopmentService/LoggerHelper.h"
#include "Views/SquashMatchStartGameView.h"
#include "Views/SquashGameOverView.h"
#include "DeviceMode/Celebration/GameCelebrationView.h"
#include "DeviceMode/Intro/MatchIntroView.h"
#include "Views/SquashGamePlayingView.h"
#include "Views/SquashTournamentChoosePlayersView.h"

class SquashMode final : public DeviceMode {
    SquashModeState state = SquashModeState::Init;
    SquashModeState previousState = SquashModeState::Init;
    Tournament tournament;
    std::vector<UserProfile *> &users;
    std::function<void(CelebrationVariant)> onMatchOver;
    // Spec 20's roster stage and the unsynced warning; null in the host tests.
    const TournamentContext *sync;
    // GAME_OVER's game: SET_SIDE on CELEBRATION moves the active match to the next pair.
    const Match *celebratedMatch = nullptr;

    bool onTournamentScreen() const {
        return state == SquashModeState::MatchStartGame || state == SquashModeState::MatchIntro
               || state == SquashModeState::GamePlaying
               || state == SquashModeState::GameCelebration || state == SquashModeState::GameOver;
    }

    // Fob C on CHOOSE_PLAYERS: to MENU, which drops the tournament (spec 20.6) - after a
    // warning while it is unsynced.
    void leave() {
        TournamentSync::leave(sync, tournament, onDeviceModeChange);
    }

    void setState(const SquashModeState newState) {
        state = newState;
    }

    void handleStateChange() {
        // START (fob or watch) is the only way out of the choose list forward.
        if (previousState == SquashModeState::TournamentChoosePlayers && state == SquashModeState::MatchStartGame) {
            // A joined roster is the peer's: the board's own rematch selection stays.
            if (!tournament.isJoined()) SelectionStore::save(SelectionStore::Sport::Squash, tournament.getPlayers());
            tournament.start(static_cast<uint8_t>(Garmin::SportId::Squash));
        }
        previousState = state;
        // Timer-driven hand-overs (celebration, intro) call no preventTriggerForMs():
        // a press latched in between must not act on the new view's first frame.
        // Only a fob skip of the intro passes its press on.
        remoteInputManager.clearLatchesForNewView();

        switch (state) {
            case SquashModeState::TournamentChoosePlayers:
                activeView = std::make_unique<SquashTournamentChoosePlayersView>(
                    tournament,
                    users,
                    [this](DeviceModeState) { leave(); },
                    [this](const SquashModeState newState) { setState(newState); }
                );
                break;
            case SquashModeState::MatchStartGame:
                activeView = std::make_unique<SquashMatchStartGameView>(
                    tournament,
                    onDeviceModeChange,
                    [this](const SquashModeState newState) { setState(newState); }
                );
                break;
            case SquashModeState::MatchIntro:
                activeView = std::make_unique<MatchIntroView<SquashModeState>>(
                    tournament,
                    [this](const SquashModeState newState) { setState(newState); },
                    Str::MATCH_SCORE_LABEL_SETS,
                    true
                );
                break;
            case SquashModeState::GamePlaying:
                activeView = std::make_unique<SquashGamePlayingView>(
                    tournament,
                    [this](const SquashModeState newState) { setState(newState); }
                );
                break;
            case SquashModeState::GameCelebration: {
                auto view = std::make_unique<GameCelebrationView<SquashModeState>>(
                    tournament,
                    [this](const SquashModeState newState) { setState(newState); },
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
            case SquashModeState::GameOver:
                activeView = std::make_unique<SquashGameOverView>(
                    celebratedMatch,
                    [this](const SquashModeState newState) { setState(newState); }
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
    SquashMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        EInkDisplay &einkDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::vector<UserProfile *> &users,
        std::function<void(CelebrationVariant)> onMatchOver,
        const TournamentContext *sync = nullptr,
        const TournamentJoin *join = nullptr
    )
        : DeviceMode(ledDisplay, backDisplay, einkDisplay, remoteInputManager, onDeviceModeChange),
          tournament(std::make_unique<SquashRules>()),
          users(users), onMatchOver(std::move(onMatchOver)), sync(sync) {

        ledDisplay.setSameSideMode(false);
        backDisplay.setSameSideMode(false);
        if (join != nullptr
            && tournament.join(join->tournamentId, join->sport, join->count, join->rosterCrc, join->entries)) {
            // A peer's tournament (spec 20.3): every entry selected, straight to MATCH_START.
            setState(SquashModeState::MatchStartGame);
            return;
        }
        SelectionStore::restore(SelectionStore::Sport::Squash, tournament, users);
        setState(SquashModeState::TournamentChoosePlayers);
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
        return state != SquashModeState::TournamentChoosePlayers;
    }

    // setState() swaps the view at the next loop(); until then a watch command gets BUSY.
    bool viewChangePending() const override {
        return state != previousState;
    }

    // Spec 20.6: a peer's BACK does not leave a tournament with unsynced changes.
    Garmin::AckStatus goBackFromWatch() override {
        if (state == SquashModeState::TournamentChoosePlayers) {
            return TournamentSync::leaveFromWatch(tournament, onDeviceModeChange);
        }
        return DeviceMode::goBackFromWatch();
    }

    bool goBack() override {
        switch (state) {
            case SquashModeState::TournamentChoosePlayers:
                leave();
                return true;
            case SquashModeState::MatchStartGame:
                // The tournament stays; START again appends the newly selected (spec 20.3).
                setState(SquashModeState::TournamentChoosePlayers);
                return true;
            case SquashModeState::MatchIntro:
                // No game exists yet (GamePlaying creates it), so nothing is lost.
                tournament.releaseMatchSlot();
                setState(SquashModeState::MatchStartGame);
                return true;
            case SquashModeState::GameCelebration:
            case SquashModeState::GameOver:
                // The result is already recorded; back cannot undo it, so it lands where
                // the forward action would.
                setState(SquashModeState::MatchStartGame);
                return true;
            case SquashModeState::GamePlaying:
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

#endif //SQUASH_MODE_H
