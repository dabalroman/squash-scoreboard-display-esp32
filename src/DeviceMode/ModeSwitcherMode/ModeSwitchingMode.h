#ifndef MODE_SWITCHING_MODE_H
#define MODE_SWITCHING_MODE_H

#include "PreferencesManager.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Views/ModeSwitchingView.h"

class ModeSwitchingMode final : public DeviceMode {
    std::unique_ptr<View> activeView;

public:
    ModeSwitchingMode(
        GlyphDisplay &glyphDisplay,
        BackDisplay &backDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange
    )
        : DeviceMode(glyphDisplay, backDisplay, remoteInputManager, onDeviceModeChange)
    {
        glyphDisplay.initForConfigMode();
        backDisplay.initSmallFont();

        activeView.reset(new ModeSwitchingView(onDeviceModeChange));
    }

    void loop() override {
        if (activeView) {
            activeView->handleInput(remoteInputManager);
            activeView->renderGlyphs(glyphDisplay);
            activeView->renderScreen(backDisplay);
        }
    }
};

#endif //MODE_SWITCHING_MODE_H
