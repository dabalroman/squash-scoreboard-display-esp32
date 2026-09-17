#ifndef BASEDEVICEMODE_H
#define BASEDEVICEMODE_H

#include <memory>

#include "DeviceModeState.h"
#include "View.h"
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
