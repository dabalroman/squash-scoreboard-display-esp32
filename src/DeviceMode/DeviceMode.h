#ifndef BASEDEVICEMODE_H
#define BASEDEVICEMODE_H

#include "DeviceModeState.h"
#include "../Display/LedDisplay/LedDisplay.h"
#include "Display/BackDisplay.h"
#include "RemoteInput/RemoteInputManager.h"

class DeviceMode {
protected:
    LedDisplay &ledDisplay;
    BackDisplay &backDisplay;
    RemoteInputManager &remoteInputManager;
    std::function<void(DeviceModeState)> onDeviceModeChange;

public:
    DeviceMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange
    ) : ledDisplay(ledDisplay), backDisplay(backDisplay), remoteInputManager(remoteInputManager), onDeviceModeChange(onDeviceModeChange) {
    }

    virtual ~DeviceMode() = default;

    virtual void loop() {
    }
};

#endif //BASEDEVICEMODE_H
