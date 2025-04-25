#ifndef SQUASHMODE_H
#define SQUASHMODE_H
#include "DeviceMode/DeviceMode.h"

class SquashMode final : public DeviceMode {
public:
    SquashMode(GlyphDisplay &glyphDisplay, Adafruit_SSD1306 &backDisplay, RemoteInputManager &remoteInputManager)
        : DeviceMode(glyphDisplay, backDisplay, remoteInputManager) {
    }

    void loop() {
    }
};

#endif //SQUASHMODE_H
