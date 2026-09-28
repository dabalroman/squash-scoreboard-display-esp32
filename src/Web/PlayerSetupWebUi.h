#ifndef PLAYER_SETUP_WEB_UI_H
#define PLAYER_SETUP_WEB_UI_H

/**
 * The device's web UI, served over the setup AP. One implementation on both
 * boards - no `#if BOARD_REV`; the only thing V1 lacks is the e-paper QR placard,
 * which has its own hardware-wrapper stub (EInkDisplay).
 *
 * The pages are real files under web/ (Polish-only, real diacritics, not routed
 * through Strings.h), gzipped into the image by helpers/web_assets.py and served
 * by WebAssets. They are static; their data comes from /api/roster, /api/settings
 * and /api/device.
 * Every route and its access rule is in the one table in registerRoutes().
 *
 * Ownership, and why no route lambda may capture a view: WebServer (core 2.0.17)
 * has no removeHandler, so a handler lives as long as the server object, while a
 * mode change destroys the outgoing mode. This object therefore lives at file
 * scope in main.cpp and its handlers capture only `this`.
 *
 * Handlers run synchronously from RemoteDevelopmentService::loop(), on the Arduino
 * loop task, so there is no locking. That loop runs before the 50 ms frame gate
 * and during overlays, which is why the gate is required and not cosmetic: a
 * request arriving after the screen closed must be refused.
 *
 * Access: gated() (see below) opens on PROFILE (`active`) OR Dev Mode + STA - see
 * WebAccessGate::open(), the one place that rule is expressed. The PROFILE idle
 * timer (`lastActivity`, `noteActivity()`) is untouched by the Dev-Mode path: it
 * exists to close the setup AP, which a Dev-Mode session never opened.
 */

#include <stddef.h>
#include <stdint.h>
#include <functional>

#include "Web/PreviewState.h"

class WebServer;
class PlayerRoster;
class PreferencesManager;

class PlayerSetupWebUi {
public:
    // previewBrightness sets the LEDs' brightness without storing it - main.cpp's
    // LedDisplay::setBrightness, so the low-battery cap still holds.
    PlayerSetupWebUi(
        PlayerRoster &roster,
        PreferencesManager &preferencesManager,
        const std::function<void(uint8_t)> &previewBrightness
    )
        : roster(roster), preferencesManager(preferencesManager), previewBrightness(previewBrightness) {
    }

    // Called once per WebServer object, from RemoteDevelopmentService::setupOTA().
    void registerRoutes(WebServer &webServer);

    // Backed by RemoteDevelopmentService::isStaConnected() - injected rather than
    // this class knowing about that one, so the gate (WebAccessGate::open()) can
    // also open outside PROFILE: Dev Mode on and already joined to the house
    // network. Set once from main.cpp; unset means "never Dev Mode reachable".
    void setStaConnectedCheck(const std::function<bool()> &check) {
        staConnected = check;
    }

    void open(uint32_t nowMs);
    void close();

    // ---- LED preview state, read by PlayerSetupView ------------------------

    // Also set by a brightness preview: FastLED applies it only on the next show().
    bool takePreviewDirty() {
        const bool dirty = preview.takeDirty() || redrawRequested;
        redrawRequested = false;
        return dirty;
    }

    bool hasPreview() const {
        return preview.hasPreview();
    }

    const PreviewSlot &previewSlot(const uint8_t side) const {
        return preview.slot(side);
    }

    uint32_t lastActivityMs() const {
        return lastActivity;
    }

    /**
     * Called by whatever else counts as someone using the editor - the firmware
     * upload does, and it is the slow case: a fumbling recipient who opens PROFILE
     * and then works only on the update screen must not have the AP closed under
     * them by the 15-minute idle timer.
     */
    void noteActivity();

    /**
     * Pumped from main.cpp right after the networking loop. The restart is
     * deferred by a few hundred ms so the socket flushes and the phone sees the
     * confirmation instead of a dropped connection.
     */
    void loop();

private:
    typedef void (PlayerSetupWebUi::*Handler)();

    PlayerRoster &roster;
    PreferencesManager &preferencesManager;
    std::function<void(uint8_t)> previewBrightness;
    WebServer *server = nullptr;

    bool active = false;
    uint32_t lastActivity = 0;
    bool restartArmed = false;
    uint32_t restartAtMs = 0;

    PreviewState preview;
    // A /settings/preview brightness is on the LEDs; the stored one must come back.
    bool brightnessPreviewActive = false;
    bool redrawRequested = false;
    // A safety net for that preview outside PROFILE: there is no close() to revert
    // it there, so loop() reverts on its own after this many idle ms.
    uint32_t lastPreviewMs = 0;

    // Empty until setStaConnectedCheck() runs (main.cpp, right after
    // RemoteDevelopmentService exists) - so a route registered before that call
    // still fails closed outside PROFILE rather than reading a null function.
    std::function<bool()> staConnected;

    bool accessAllowed() const;

    std::function<void()> gated(Handler handler);
    std::function<void()> page(const char *path);
    std::function<void()> asset(const char *path);

    void armRestart();
    void reject(const char *reason);
    void sendJson(const char *json, size_t length);

    void handleRoster();
    void handleDevice();
    void handleSave();
    void handlePreview();
    void handleSettings();
    void handleSaveSettings();
    void handleSettingsPreview();
    void handleSaveDev();
    void revertBrightnessPreview();
};

#endif //PLAYER_SETUP_WEB_UI_H
