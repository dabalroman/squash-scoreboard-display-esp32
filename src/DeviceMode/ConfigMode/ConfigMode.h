#ifndef CONFIG_MODE_H
#define CONFIG_MODE_H

#include "BatterySensor.h"
#include "PreferencesManager.h"
#include "Utils.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Views/ConfigView.h"

class ConfigMode final : public DeviceMode {
    PreferencesManager &preferencesManager;
    std::unique_ptr<View> activeView;

public:
    ConfigMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        PreferencesManager &preferencesManager,
        const BatterySensor &batterySensor
    )
        : DeviceMode(ledDisplay, backDisplay, remoteInputManager, onDeviceModeChange),
          preferencesManager(preferencesManager) {

        activeView = std::make_unique<ConfigView>(preferencesManager, onDeviceModeChange, batterySensor);
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

#endif //CONFIG_MODE_H
