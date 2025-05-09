#ifndef CONFIG_MODE_H
#define CONFIG_MODE_H

#include "PreferencesManager.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Views/ConfigView.h"

class ConfigMode final : public DeviceMode {
    PreferencesManager &preferencesManager;
    std::unique_ptr<View> activeView;

public:
    ConfigMode(
        GlyphDisplay &glyphDisplay,
        BackDisplay &backDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        PreferencesManager &preferencesManager
    )
        : DeviceMode(glyphDisplay, backDisplay, remoteInputManager, onDeviceModeChange),
          preferencesManager(preferencesManager) {
        glyphDisplay.initForConfigMode();
        backDisplay.initForConfigMode();

        activeView.reset(new ConfigView(preferencesManager, onDeviceModeChange));
    }

    void loop() override {
        if (activeView) {
            activeView->handleInput(remoteInputManager);
            activeView->renderGlyphs(glyphDisplay);
            activeView->renderScreen(backDisplay);
        }
    }
};

#endif //CONFIG_MODE_H
