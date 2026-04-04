#ifndef MODE_SWITCHING_MODE_H
#define MODE_SWITCHING_MODE_H

#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Views/ModeSwitchingView.h"

class ModeSwitchingMode final : public DeviceMode {
    std::unique_ptr<View> activeView;

public:
    ModeSwitchingMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange
    )
        : DeviceMode(ledDisplay, backDisplay, remoteInputManager, onDeviceModeChange) {
        ledDisplay.initForConfigMode();
        backDisplay.initSmallFont();

        activeView = std::make_unique<ModeSwitchingView>(onDeviceModeChange);
        activeView->initLedDisplay(ledDisplay);
        activeView->initBackDisplay(backDisplay);
    }

    void loop() override {
        if (activeView) {
            activeView->handleInput(remoteInputManager);
            activeView->renderLedDisplay(ledDisplay);
            activeView->renderBackDisplay(backDisplay);
        }
    }
};

#endif //MODE_SWITCHING_MODE_H
