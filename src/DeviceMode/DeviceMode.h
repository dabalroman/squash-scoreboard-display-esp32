#ifndef BASEDEVICEMODE_H
#define BASEDEVICEMODE_H

#include <memory>

#include "DeviceModeState.h"
#include "View.h"
#include "WatchSupport.h"
#include "Garmin/WatchCommand.h"
#include "Garmin/WatchState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/BackDisplay.h"
#include "Display/EInk/EInkDisplay.h"
#include "RemoteInput/RemoteInputManager.h"

class Tournament;

class DeviceMode {
protected:
    LedDisplay &ledDisplay;
    BackDisplay &backDisplay;
    EInkDisplay &einkDisplay;
    RemoteInputManager &remoteInputManager;
    std::function<void(DeviceModeState)> onDeviceModeChange;

    // Owned here so anything holding a DeviceMode can put the active view back on
    // screen without knowing which mode it is.
    std::unique_ptr<View> activeView;

public:
    DeviceMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        EInkDisplay &einkDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange
    ) : ledDisplay(ledDisplay), backDisplay(backDisplay), einkDisplay(einkDisplay), remoteInputManager(remoteInputManager), onDeviceModeChange(onDeviceModeChange) {
    }

    virtual ~DeviceMode() = default;

    virtual void loop() = 0;

    /**
     * True where the "in match" buzzer mode sounds: a sport mode past its player selector.
     * Pure, so a new mode has to state its answer.
     */
    virtual bool isInMatch() const = 0;

    /**
     * Long-press back: step one level up this mode's state machine. false means there is
     * nowhere to go back to from here, and main.cpp then stays silent - that silence is
     * how the user is told this screen is a root.
     */
    virtual bool goBack() {
        return false;
    }

    // The watch's BACK: goBack(), unless a screen refuses it (INVALID) or leaves something only
    // a person may leave.
    virtual Garmin::AckStatus goBackFromWatch() {
        return goBack() ? Garmin::AckStatus::Applied : Garmin::AckStatus::WrongScreen;
    }

    /**
     * A mode that defers its view swap to its next loop() (setState) says so here: the
     * view still on screen is then the outgoing one, and a watch command gets BUSY.
     */
    virtual bool viewChangePending() const {
        return false;
    }

    /**
     * BACK is the long-C chain and offline games edit the tournament, so both live here;
     * everything else belongs to the view. GamePlaying's goBack() is false, which is what
     * keeps a watch from discarding a game.
     */
    virtual Garmin::AckStatus handleWatchCommand(const WatchCommand &command) {
        return WatchSupport::route(
            command,
            viewChangePending() || !activeView,
            [this] { return goBackFromWatch(); },
            [this, &command] {
                const Garmin::AckStatus status = editOfflineGames(command);
                if (status == Garmin::AckStatus::Applied) activeView->onTournamentEdited();
                return status;
            },
            [this, &command] {
                const Garmin::AckStatus status = handleTournamentCommand(command);
                if (status != Garmin::AckStatus::Applied) return status;
                // The view's cached match lost its Game to the new pair's match.
                if (command.id == Garmin::CommandId::SetSide) {
                    if (activeView->refreshPair()) restoreView();
                    return status;
                }
                // A roster growth renames or adds entries.
                if (command.id == Garmin::CommandId::BeginTournament) {
                    tournamentEdited();
                }
                return status;
            },
            [this, &command] { return activeView->handleWatchCommand(command); }
        );
    }

    // ADD / REMOVE_OFFLINE_GAME: a sport mode on a tournament screen applies them to its
    // tournament (WatchSupport::editOfflineGames); everywhere else WRONG_SCREEN.
    virtual Garmin::AckStatus editOfflineGames(const WatchCommand &) {
        return Garmin::AckStatus::WrongScreen;
    }

    // BEGIN_TOURNAMENT (roster growth), ACK_CHANGES, SET_SIDE (spec 20): the sport modes take
    // them (TournamentSync); everywhere else WRONG_SCREEN. BEGIN on MENU is main.cpp's.
    virtual Garmin::AckStatus handleTournamentCommand(const WatchCommand &) {
        return Garmin::AckStatus::WrongScreen;
    }

    // The sport modes' tournament, for RESULTS and TOURNAMENT (spec 19, 20); null elsewhere.
    virtual const Tournament *getTournament() const {
        return nullptr;
    }

    // The same, for a RECORD write's merge and a writer reset (main.cpp); null elsewhere.
    virtual Tournament *editTournament() {
        return nullptr;
    }

    // Records or roster changed outside the view: standings and names are drawn again.
    void tournamentEdited() {
        if (activeView) {
            activeView->onTournamentEdited();
            activeView->queueRender();
        }
    }

    virtual void describeForWatch(WatchState &state) const {
        if (activeView) {
            activeView->describeForWatch(state);
        } else {
            state.setBusy(Garmin::ScreenId::Booting);
        }
    }

    // Device-level OLED chrome changed (the watch badge); the view's state is untouched.
    void queueBackRender() {
        if (activeView) {
            activeView->queueBackRender();
        }
    }

    /**
     * Redraw the active view from scratch after something else owned the displays
     * (a device-level overlay). The mode's state machine is untouched: this only
     * re-applies the view's display setup and marks everything dirty. The e-paper
     * redraws on its own, through the content hash.
     */
    void restoreView() {
        if (!activeView) {
            return;
        }

        activeView->initLedDisplay(ledDisplay);
        activeView->initBackDisplay(backDisplay);
        activeView->initEInkDisplay(einkDisplay);
        activeView->queueRender();
    }
};

#endif //BASEDEVICEMODE_H
