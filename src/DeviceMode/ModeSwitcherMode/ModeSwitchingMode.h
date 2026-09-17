#ifndef MODE_SWITCHING_MODE_H
#define MODE_SWITCHING_MODE_H

#include "BatteryMonitor.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Views/ModeSwitchingView.h"

class ModeSwitchingMode final : public DeviceMode {

public:
    ModeSwitchingMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        EInkDisplay &einkDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        const BatteryMonitor &batteryMonitor
    )
        : DeviceMode(ledDisplay, backDisplay, einkDisplay, remoteInputManager, onDeviceModeChange) {

        activeView = std::make_unique<ModeSwitchingView>(onDeviceModeChange, batteryMonitor);
        activeView->initLedDisplay(ledDisplay);
        activeView->initBackDisplay(backDisplay);
        activeView->initEInkDisplay(einkDisplay);
    }

    void loop() override {
        if (activeView) {
            activeView->handleInput(remoteInputManager);
            activeView->renderLedDisplay(ledDisplay);
            activeView->renderBackDisplay(backDisplay);
            activeView->renderEInkDisplay(einkDisplay);
        }
    }
};

#endif //MODE_SWITCHING_MODE_H
