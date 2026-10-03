#ifndef PLAYER_SETUP_MODE_H
#define PLAYER_SETUP_MODE_H

#include "DeviceMode/DeviceMode.h"
#include "Web/PlayerSetupWebUi.h"
#include "RemoteDevelopmentService/RemoteDevelopmentService.h"
#include "Views/PlayerSetupView.h"
#include "Utils.h"

/**
 * The roster editor. Entering forces the setup AP up whatever `enableDevMode` says -
 * it is an explicit user action, not a background service.
 *
 * The AP and the HTTP gate are opened in the constructor and closed in the
 * destructor, deliberately not in the D handler: changeDeviceMode() destroys this
 * mode from inside a lambda the mode owns, so the destructor is the one hook that
 * every exit path - D, C, the idle timeout, anything added later - is guaranteed
 * to run through. The web routes themselves outlive the mode (WebServer has no
 * removeHandler), which is why they refuse requests while the gate is shut.
 */
class PlayerSetupMode final : public DeviceMode {
    RemoteDevelopmentService &remoteDevelopmentService;
    PlayerSetupWebUi &webUi;
    PlayerSetupView *view = nullptr;

public:
    PlayerSetupMode(
        LedDisplay &ledDisplay,
        BackDisplay &backDisplay,
        EInkDisplay &einkDisplay,
        RemoteInputManager &remoteInputManager,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        RemoteDevelopmentService &remoteDevelopmentService,
        PlayerSetupWebUi &webUi
    )
        : DeviceMode(ledDisplay, backDisplay, einkDisplay, remoteInputManager, onDeviceModeChange),
          remoteDevelopmentService(remoteDevelopmentService), webUi(webUi) {

        remoteDevelopmentService.enablePlayerSetupAp();
        webUi.open(millis());

        std::unique_ptr<PlayerSetupView> setupView =
            std::make_unique<PlayerSetupView>(webUi, remoteDevelopmentService, onDeviceModeChange);
        view = setupView.get();
        activeView = std::move(setupView);
        activeView->initLedDisplay(ledDisplay);
        activeView->initBackDisplay(backDisplay);
        activeView->initEInkDisplay(einkDisplay);
    }

    ~PlayerSetupMode() override {
        webUi.close();
        remoteDevelopmentService.disablePlayerSetupAp();
    }

    bool isInMatch() const override {
        return false;
    }

    /**
     * A watch's BACK is the C/D exit (spec 10.2), deliberately not a goBack() override:
     * that would also make a long C here leave PROFILE, where today it is a silent no-op.
     */
    Garmin::AckStatus handleWatchCommand(const WatchCommand &command) override {
        if (command.id != Garmin::CommandId::Back) {
            return Garmin::AckStatus::WrongScreen;
        }

        printLn("Player setup: closed from a watch");
        view->exitToMenu();
        return Garmin::AckStatus::Applied;
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

#endif //PLAYER_SETUP_MODE_H
