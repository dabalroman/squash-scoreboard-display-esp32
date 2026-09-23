#ifndef PLAYER_SETUP_VIEW_H
#define PLAYER_SETUP_VIEW_H

#include "Strings.h"
#include "DeviceMode/DeviceModeState.h"
#include "DeviceMode/View.h"
#include "Display/EInk/Images/PlayerSetupQr.h"
#include "Web/PlayerSetupWebUi.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "RemoteDevelopmentService/LoggerHelper.h"
#include "RemoteDevelopmentService/RemoteDevelopmentService.h"

/**
 * The screen the roster editor shows while its AP is up: a static word on the
 * front LEDs, the dual-QR placard on the e-paper (V2 only - EInkDisplay stubs to
 * nothing on V1), and an auto-scrolling OLED discovery screen shared by both
 * boards - the AP name, its password and the current IP, for whoever has no
 * e-paper (or no QR reader) to fall back on.
 *
 * The AP itself is raised and dropped by PlayerSetupMode, not here, so every exit
 * path - D, C, the idle timeout, or anything added later - tears it down the same
 * way. A save reboots on its own, driven from PlayerSetupWebUi::loop().
 */
class PlayerSetupView final : public View {
    PlayerSetupWebUi &webUi;
    RemoteDevelopmentService &remoteDevelopmentService;
    std::function<void(DeviceModeState)> onDeviceModeChange;

    uint32_t lastRemoteMs;

    // Long enough to walk away, find a phone and type; short enough that a
    // forgotten screen does not leave an open AP up all evening.
    enum : uint32_t { IDLE_TIMEOUT_MS = 900000 };

    // Title fixed on line 0; lines 1-2 show one label/value pair at a time, the
    // next pair every SCROLL_STEP_MS - stepping a line at a time put a value over
    // the next pair's label. Values are read fresh each render, not cached, since
    // the IP is only known once the AP has actually come up.
    enum : uint8_t {
        SCROLL_WIFI_LABEL = 0,
        SCROLL_SSID,
        SCROLL_PASSWORD_LABEL,
        SCROLL_PASSWORD,
        SCROLL_IP_LABEL,
        SCROLL_IP,
        SCROLL_ITEM_COUNT
    };
    static constexpr uint32_t SCROLL_STEP_MS = 1500;

    uint8_t scrollIndex = 0;
    uint32_t lastScrollMs = 0;
    bool scrollStarted = false;

    String scrollItem(const uint8_t index) const {
        switch (index % SCROLL_ITEM_COUNT) {
            case SCROLL_WIFI_LABEL: return Str::PLAYER_SETUP_OLED_WIFI_LABEL;
            case SCROLL_SSID: return RemoteDevelopmentService::AP_SSID;
            case SCROLL_PASSWORD_LABEL: return Str::PLAYER_SETUP_OLED_PASSWORD_LABEL;
            case SCROLL_PASSWORD: return RemoteDevelopmentService::AP_PASSWORD;
            case SCROLL_IP_LABEL: return Str::PLAYER_SETUP_OLED_IP_LABEL;
            default: return remoteDevelopmentService.currentIpAddress();
        }
    }

public:
    PlayerSetupView(
        PlayerSetupWebUi &webUi,
        RemoteDevelopmentService &remoteDevelopmentService,
        const std::function<void(DeviceModeState)> &onDeviceModeChange
    )
        : webUi(webUi), remoteDevelopmentService(remoteDevelopmentService),
          onDeviceModeChange(onDeviceModeChange), lastRemoteMs(millis()) {
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

    // Per-tick: the scroll advances on its own clock, once per SCROLL_STEP_MS. A
    // queued render (restoreView() after an Overlay) still redraws at once, or the
    // overlay's text would linger for up to a whole step.
    void renderBackDisplay(BackDisplay &backDisplay) override {
        const uint32_t now = millis();
        if (!shouldRenderBack && scrollStarted && now - lastScrollMs < SCROLL_STEP_MS) {
            return;
        }
        lastScrollMs = now;
        scrollStarted = true;
        shouldRenderBack = false;

        backDisplay.clear();
        backDisplay.setCursorToLine(0, 0);
        backDisplay.print(Str::PLAYER_SETUP_OLED_TITLE);
        backDisplay.setCursorToLine(0, 1);
        backDisplay.print(scrollItem(scrollIndex));
        backDisplay.setCursorToLine(0, 2);
        backDisplay.print(scrollItem(scrollIndex + 1));
        backDisplay.display();

        scrollIndex = (scrollIndex + 2) % SCROLL_ITEM_COUNT;
    }
};

#endif //PLAYER_SETUP_VIEW_H
