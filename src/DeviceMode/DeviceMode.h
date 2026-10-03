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

    /**
     * A mode that defers its view swap to its next loop() (setState) says so here: the
     * view still on screen is then the outgoing one, and a watch command gets BUSY.
     */
    virtual bool viewChangePending() const {
        return false;
    }

    /**
     * BACK is the long-C chain, so it lives here; everything else belongs to the view.
     * GamePlaying's goBack() is false, which is what keeps a watch from discarding a game.
     */
    virtual Garmin::AckStatus handleWatchCommand(const WatchCommand &command) {
        return WatchSupport::route(
            command,
            viewChangePending() || !activeView,
            [this] { return goBack(); },
            [this, &command] { return activeView->handleWatchCommand(command); }
        );
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
