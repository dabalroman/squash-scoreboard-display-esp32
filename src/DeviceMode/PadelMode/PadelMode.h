#ifndef PADEL_MODE_H
#define PADEL_MODE_H

#include <memory>
#include "PadelModeState.h"
#include "Utils.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/TournamentSync.h"
#include "DeviceMode/View.h"
#include "Tournament/Tournament.h"
#include "Tournament/SelectionStore.h"
#include "Tournament/Rules/PadelRules.h"
#include "RemoteDevelopmentService/LoggerHelper.h"
#include "Views/PadelMatchStartGameView.h"
#include "Views/PadelGameOverView.h"
#include "DeviceMode/Celebration/GameCelebrationView.h"
#include "DeviceMode/Intro/MatchIntroView.h"
#include "Views/PadelGamePlayingView.h"
#include "Views/PadelTournamentChoosePlayersView.h"

class PadelMode final : public DeviceMode {
    PadelModeState state = PadelModeState::Init;
    PadelModeState previousState = PadelModeState::Init;
    Tournament tournament;
    std::vector<UserProfile *> &users;
    std::function<void(CelebrationVariant)> onMatchOver;
    // Spec 20's roster stage and the unsynced warning; null in the host tests.
    const TournamentContext *sync;
    // GAME_OVER's game: SET_SIDE on CELEBRATION moves the active match to the next pair.
    const Match *celebratedMatch = nullptr;

    bool onTournamentScreen() const {
        return state == PadelModeState::MatchStartGame || state == PadelModeState::MatchIntro
               || state == PadelModeState::GamePlaying
               || state == PadelModeState::GameCelebration || state == PadelModeState::GameOver;
    }

    // Fob C on CHOOSE_PLAYERS: to MENU, which drops the tournament (spec 20.6) - after a
    // warning while it is unsynced.
    void leave() {
        TournamentSync::leave(sync, tournament, onDeviceModeChange);
    }

    void setState(const PadelModeState newState) {
        state = newState;
    }

    void handleStateChange() {
        // START (fob or watch) is the only way out of the choose list forward.
        if (previousState == PadelModeState::TournamentChoosePlayers && state == PadelModeState::MatchStartGame) {
            // A joined roster is the peer's: the board's own rematch selection stays.
            if (!tournament.isJoined()) SelectionStore::save(SelectionStore::Sport::Padel, tournament.getPlayers());
            tournament.start(static_cast<uint8_t>(Garmin::SportId::Padel));
        }
        previousState = state;
        // Timer-driven hand-overs (celebration, intro) call no preventTriggerForMs():
        // a press latched in between must not act on the new view's first frame.
        // Only a fob skip of the intro passes its press on.
        remoteInputManager.clearLatchesForNewView();

        switch (state) {
            case PadelModeState::TournamentChoosePlayers:
                activeView = std::make_unique<PadelTournamentChoosePlayersView>(
                    tournament,
                    users,
                    [this](DeviceModeState) { leave(); },
                    [this](const PadelModeState newState) { setState(newState); }
                );
                break;
            case PadelModeState::MatchStartGame:
                activeView = std::make_unique<PadelMatchStartGameView>(
                    tournament,
                    onDeviceModeChange,
                    [this](const PadelModeState newState) { setState(newState); }
                );
                break;
            case PadelModeState::MatchIntro:
                activeView = std::make_unique<MatchIntroView<PadelModeState>>(
                    tournament,
                    [this](const PadelModeState newState) { setState(newState); },
                    Str::MATCH_SCORE_LABEL_GEMS,
                    false
                );
                break;
            case PadelModeState::GamePlaying:
                activeView = std::make_unique<PadelGamePlayingView>(
                    tournament,
                    [this](const PadelModeState newState) { setState(newState); }
                );
                break;
            case PadelModeState::GameCelebration: {
                auto view = std::make_unique<GameCelebrationView<PadelModeState>>(
                    tournament,
                    [this](const PadelModeState newState) { setState(newState); },
                    Str::MATCH_SCORE_LABEL_GEMS,
                    true
                );
                celebratedMatch = tournament.getActiveMatch();
                // Once per game: restoreView() after an Overlay never comes through here.
                if (onMatchOver) {
                    onMatchOver(view->getVariant());
                }

                activeView = std::move(view);
                break;
            }
            case PadelModeState::GameOver:
                activeView = std::make_unique<PadelGameOverView>(
                    celebratedMatch,
                    [this](const PadelModeState newState) { setState(newState); }
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
    PadelMode(
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
          tournament(std::make_unique<PadelRules>()),
          users(users), onMatchOver(std::move(onMatchOver)), sync(sync) {

        ledDisplay.setSameSideMode(true);
        backDisplay.setSameSideMode(true);
        if (join != nullptr
            && tournament.join(join->tournamentId, join->sport, join->count, join->rosterCrc, join->entries)) {
            // A peer's tournament (spec 20.3): every entry selected, straight to MATCH_START.
            setState(PadelModeState::MatchStartGame);
            return;
        }
        SelectionStore::restore(SelectionStore::Sport::Padel, tournament, users);
        setState(PadelModeState::TournamentChoosePlayers);
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
        return state != PadelModeState::TournamentChoosePlayers;
    }

    // setState() swaps the view at the next loop(); until then a watch command gets BUSY.
    bool viewChangePending() const override {
        return state != previousState;
    }

    // Spec 20.6: a peer's BACK does not leave a tournament with unsynced changes.
    Garmin::AckStatus goBackFromWatch() override {
        if (state == PadelModeState::TournamentChoosePlayers) {
            return TournamentSync::leaveFromWatch(tournament, onDeviceModeChange);
        }
        return DeviceMode::goBackFromWatch();
    }

    bool goBack() override {
        switch (state) {
            case PadelModeState::TournamentChoosePlayers:
                leave();
                return true;
            case PadelModeState::MatchStartGame:
                // The tournament stays; START again appends the newly selected (spec 20.3).
                setState(PadelModeState::TournamentChoosePlayers);
                return true;
            case PadelModeState::MatchIntro:
                // No game exists yet (GamePlaying creates it), so nothing is lost.
                tournament.releaseMatchSlot();
                setState(PadelModeState::MatchStartGame);
                return true;
            case PadelModeState::GameCelebration:
            case PadelModeState::GameOver:
                // The result is already recorded; back cannot undo it, so it lands where
                // the forward action would.
                setState(PadelModeState::MatchStartGame);
                return true;
            case PadelModeState::GamePlaying:
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

#endif //PADEL_MODE_H
