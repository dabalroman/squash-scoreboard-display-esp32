#ifndef LAYER_DEMO_MODE_H
#define LAYER_DEMO_MODE_H

#include "DeviceMode/DeviceMode.h"
#include "Views/LayerDemoView.h"

/**
 * Temporary (#52), V1 only: the GameOver screen with the celebration looping over
 * it, to pick the celebration's blend on the real LEDs. Deleted once chosen.
 */
class LayerDemoMode final : public DeviceMode {
public:
    LayerDemoMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        EInkDisplay &einkDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange
    )
        : DeviceMode(ledDisplay, backDisplay, einkDisplay, remoteInputManager, onDeviceModeChange) {

        activeView = std::make_unique<LayerDemoView>();
        activeView->initLedDisplay(ledDisplay);
        activeView->initBackDisplay(backDisplay);
        activeView->initEInkDisplay(einkDisplay);
    }

    ~LayerDemoMode() override {
        // The picked blend stays, so a real game can be checked with it; breathing does not.
        ledDisplay.setBreathing(false);
    }

    bool goBack() override {
        onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
        return true;
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

#endif //LAYER_DEMO_MODE_H
