#ifndef BASEDEVICEMODE_H
#define BASEDEVICEMODE_H

#include "RemoteInputManager.h"
#include "Display/GlyphDisplay.h"
#include "Display/BackDisplay.h"

class DeviceMode {
protected:
    GlyphDisplay &glyphDisplay;
    BackDisplay &backDisplay;
    RemoteInputManager &remoteInputManager;

public:
    DeviceMode(
        GlyphDisplay &glyphDisplay,
        BackDisplay &backDisplay,
        RemoteInputManager &remoteInputManager
    ) : glyphDisplay(glyphDisplay), backDisplay(backDisplay), remoteInputManager(remoteInputManager) {
    }

    virtual ~DeviceMode() = default;

    virtual void loop() {
    }
};

#endif //BASEDEVICEMODE_H
