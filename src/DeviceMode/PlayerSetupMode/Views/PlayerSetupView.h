#ifndef PLAYER_SETUP_VIEW_H
#define PLAYER_SETUP_VIEW_H

#include "Strings.h"
#include "DeviceMode/DeviceModeState.h"
#include "DeviceMode/View.h"
#include "Display/EInk/Images/PlayerSetupQr.h"
#include "Display/EInk/PlayerSetupWebUi.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "RemoteDevelopmentService/LoggerHelper.h"

/**
 * The screen the roster editor shows while its AP is up: the dual-QR placard on
 * the e-paper, a static word on the front LEDs and the remote hints on the OLED.
 *
 * The AP itself is raised and dropped by PlayerSetupMode, not here, so every exit
 * path - D, C, the idle timeout, or anything added later - tears it down the same
 * way. A save reboots on its own, driven from PlayerSetupWebUi::loop().
 */
class PlayerSetupView final : public View {
    PlayerSetupWebUi &webUi;
    std::function<void(DeviceModeState)> onDeviceModeChange;

    uint32_t lastRemoteMs;

    // Long enough to walk away, find a phone and type; short enough that a
    // forgotten screen does not leave an open AP up all evening.
    enum : uint32_t { IDLE_TIMEOUT_MS = 900000 };

public:
    PlayerSetupView(
        PlayerSetupWebUi &webUi,
        const std::function<void(DeviceModeState)> &onDeviceModeChange
    )
        : webUi(webUi), onDeviceModeChange(onDeviceModeChange), lastRemoteMs(millis()) {
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        // Wall-clock, not a tick count: an overlay pausing the mode only delays
        // when an expired deadline is noticed, it never extends it.
        const uint32_t webActivity = webUi.lastActivityMs();
        const uint32_t idleSince = webActivity > lastRemoteMs ? webActivity : lastRemoteMs;

        if (millis() - idleSince >= IDLE_TIMEOUT_MS) {
            printLn("Player setup: idle for %lu ms, closing", static_cast<unsigned long>(IDLE_TIMEOUT_MS));
            onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
            return;
        }

        // A and B do nothing here, but their latches must still be consumed or
        // they act on whatever view comes next.
        if (remoteInputManager.buttonA.takeActionIfPossible()
            || remoteInputManager.buttonB.takeActionIfPossible()) {
            lastRemoteMs = millis();
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()
            || remoteInputManager.buttonD.takeActionIfPossible()) {
            printLn("Player setup: closed from the remote");
            remoteInputManager.preventTriggerForMs();
            onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
            return;
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.resetAnimations();
        ledDisplay.setColonAppearance();
        ledDisplay.setPlayersIndicatorsState(true);
        ledDisplay.setBorderEnabled(false);
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        if (!shouldRenderLedDisplay) {
            return;
        }

        ledDisplay.setGlyphsText(Str::LED_PLAYER_SETUP);
        ledDisplay.setGlyphsColor(Colors::Aqua, Colors::Aqua);
        ledDisplay.setIndicatorAppearancePlayerA(Colors::Aqua);
        ledDisplay.setIndicatorAppearancePlayerB(Colors::Aqua);
        ledDisplay.display();

        shouldRenderLedDisplay = false;
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.initSmallFont();
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available()) {
            return;
        }

        einkDisplay.showImage(PLAYER_SETUP_QR_BITMAP);
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        backDisplay.setCursorToLine(0, 0);
        backDisplay.print(Str::PLAYER_SETUP_OLED_TITLE);
        backDisplay.setCursorToLine(0, 1);
        backDisplay.print(Str::PLAYER_SETUP_OLED_USE_EINK);
        backDisplay.setCursorToLine(0, 2);
        backDisplay.print(Str::PLAYER_SETUP_OLED_USE_EINK_2);
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //PLAYER_SETUP_VIEW_H
