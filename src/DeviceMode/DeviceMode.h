#ifndef BASEDEVICEMODE_H
#define BASEDEVICEMODE_H

#include "DeviceModeState.h"
#include "Display/GlyphDisplay.h"
#include "Display/BackDisplay.h"
#include "RemoteInput/RemoteInputManager.h"

class DeviceMode {
protected:
    GlyphDisplay &glyphDisplay;
    BackDisplay &backDisplay;
    RemoteInputManager &remoteInputManager;
    std::function<void(DeviceModeState)> onDeviceModeChange;

public:
    DeviceMode(
        GlyphDisplay &glyphDisplay,
        BackDisplay &backDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange
    ) : glyphDisplay(glyphDisplay), backDisplay(backDisplay), remoteInputManager(remoteInputManager), onDeviceModeChange(onDeviceModeChange) {
    }

    virtual ~DeviceMode() = default;

    virtual void loop() {
    }
};

#endif //BASEDEVICEMODE_H
