#ifndef BASEDEVICEMODE_H
#define BASEDEVICEMODE_H

#include "Adafruit_SSD1306.h"
#include "RemoteInputManager.h"
#include "../Display/GlyphDisplay.h"

class DeviceMode {
protected:
    GlyphDisplay &glyphDisplay;
    Adafruit_SSD1306 &backDisplay;
    RemoteInputManager &remoteInputManager;

public:
    DeviceMode(
        GlyphDisplay &glyphDisplay,
        Adafruit_SSD1306 &backDisplay,
        RemoteInputManager &remoteInputManager
    ) : glyphDisplay(glyphDisplay), backDisplay(backDisplay), remoteInputManager(remoteInputManager) {
    }

    virtual ~DeviceMode() = default;

    virtual void loop() {
    }
};

#endif //BASEDEVICEMODE_H
