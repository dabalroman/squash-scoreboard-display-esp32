#ifndef CONFIG_MODE_H
#define CONFIG_MODE_H

#include "BatteryMonitor.h"
#include "PreferencesManager.h"
#include "Utils.h"
#include "ConfigModeState.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/View.h"
#include "Garmin/GarminService.h"
#include "Views/ConfigView.h"
#include "Views/GarminView.h"
#include "Views/GarminPairingView.h"

class ConfigMode final : public DeviceMode {
    PreferencesManager &preferencesManager;
    const BatteryMonitor &batteryMonitor;
    GarminService &garminService;

    ConfigModeState state = ConfigModeState::Menu;
    ConfigModeState previousState = ConfigModeState::Menu;

    // Applied at the top of loop(), like the sport modes: the request arrives from the
    // outgoing view's handleInput(), which must not be freed under its own render calls.
    void setState(const ConfigModeState newState) {
        state = newState;
    }

    void handleStateChange() {
        const ConfigModeState from = previousState;
        previousState = state;
        remoteInputManager.clearLatches();

        // Coming back up lands on the row that went down.
        switch (state) {
            case ConfigModeState::Garmin:
                activeView = std::make_unique<GarminView>(
                    garminService, batteryMonitor,
                    [this](const ConfigModeState newState) { setState(newState); },
                    from == ConfigModeState::GarminPairing ? 1 : 0);
                break;
            case ConfigModeState::GarminPairing:
                activeView = std::make_unique<GarminPairingView>(
                    garminService,
                    [this](const ConfigModeState newState) { setState(newState); });
                break;
            case ConfigModeState::Menu:
            default:
                activeView = makeMenu(from == ConfigModeState::Garmin ? Settings::garmin : 0);
                break;
        }

        activeView->initLedDisplay(ledDisplay);
        activeView->initBackDisplay(backDisplay);
        activeView->initEInkDisplay(einkDisplay);
    }

    std::unique_ptr<View> makeMenu(const uint8_t selectedOption) {
        return std::make_unique<ConfigView>(
            preferencesManager, onDeviceModeChange, batteryMonitor,
            [this](const ConfigModeState newState) { setState(newState); },
            selectedOption);
    }

public:
    ConfigMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        EInkDisplay &einkDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        PreferencesManager &preferencesManager,
        const BatteryMonitor &batteryMonitor,
        GarminService &garminService
    )
        : DeviceMode(ledDisplay, backDisplay, einkDisplay, remoteInputManager, onDeviceModeChange),
          preferencesManager(preferencesManager), batteryMonitor(batteryMonitor), garminService(garminService) {

        activeView = makeMenu(0);
        activeView->initLedDisplay(ledDisplay);
        activeView->initBackDisplay(backDisplay);
        activeView->initEInkDisplay(einkDisplay);
    }

    bool goBack() override {
        switch (state) {
            case ConfigModeState::GarminPairing:
                setState(ConfigModeState::Garmin);
                return true;
            case ConfigModeState::Garmin:
                setState(ConfigModeState::Menu);
                return true;
            case ConfigModeState::Menu:
            default:
                // Same contract as ConfigView's exit option: settings are saved, not discarded.
                preferencesManager.save();
                onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
                return true;
        }
    }

    bool isInMatch() const override {
        return false;
    }

    void loop() override {
        if (state != previousState) {
            handleStateChange();
        }

        if (activeView) {
            activeView->handleInput(remoteInputManager);
            activeView->renderLedDisplay(ledDisplay);
            activeView->renderBackDisplay(backDisplay);
            activeView->renderEInkDisplay(einkDisplay);
        }
    }
};

#endif //CONFIG_MODE_H
