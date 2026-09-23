#ifndef REMOTE_DEVELOPMENT_SERVICE_H
#define REMOTE_DEVELOPMENT_SERVICE_H

#include <functional>
#include <memory>
#include <WiFi.h>
#include <WebServer.h>
#include "PreferencesManager.h"
#include "FirmwareImageCheck.h"
#include "Display/BackDisplay.h"

/**
 * What POST /update is doing, for whoever paints the board. `Started` fires as
 * the first bytes arrive, not at the end: WebServer reads the whole multipart
 * body inside handleClient(), so loop() is stalled for the entire upload and this
 * is the only chance to show anything while it streams.
 */
enum class FirmwareUpdateStage : uint8_t { Started, Failed, Succeeded };

class RemoteDevelopmentService {
    std::unique_ptr<WebServer> OTAServer;
    std::unique_ptr<WiFiServer> telnetServer;
    WiFiClient telnetClient;
    PreferencesManager *preferencesManager = nullptr;
    BackDisplay *backDisplay = nullptr;

    // Extra routes to hang off the port-80 server, registered once per server
    // object. Set from main.cpp so this class needs no knowledge of what registers
    // them; the callback must capture only objects with static storage duration,
    // because WebServer has no removeHandler and handlers outlive any view.
    std::function<void(WebServer &)> extraRoutes;

    // Same rule as extraRoutes: called from a route handler, so it may capture only
    // objects with static storage duration. It exists so this class never learns
    // about LedDisplay or EInkDisplay - main.cpp owns the painting.
    std::function<void(FirmwareUpdateStage, const char *)> updateStatusHandler;

    // Per-upload state for POST /update. Reset at UPLOAD_FILE_START, so a rejected
    // attempt leaves nothing behind for the next one.
    FirmwareImageCheck::Accumulator otaHeader;
    const char *otaRejectReason = nullptr;   // non-null latches the rejection: no more writes, no reboot
    const char *otaRejectLabel = nullptr;    // the short screen line for the status handler
    size_t otaBytes = 0;
    bool otaForced = false;
    bool otaVerdictLogged = false;
    bool otaTelnetClosed = false;

    // The reply is queued into a TCP segment that has not left yet, so the restart
    // waits a moment. Pumped from loop(), never a delay() in the handler: main.cpp
    // polls the e-paper BUSY pin on every pass.
    static constexpr uint32_t OTA_RESTART_DELAY_MS = 300;
    bool otaRestartArmed = false;
    uint32_t otaRestartAtMs = 0;

    bool isAPActive = false;
    bool isWifiActive = false;
    bool isTelnetActive = false;
    bool isOTAActive = false;
    bool isNTPActive = false;

    // Set by disablePlayerSetupAp() when it starts a non-blocking WiFi.begin();
    // cleared by checkStaReconnect() once WL_CONNECTED lands, or by
    // enablePlayerSetupAp() if the editor is reopened before that happens.
    bool staReconnectPending = false;
    uint32_t staReconnectStartMs = 0;
    bool staFallbackApUp = false;
    // Same budget init() gives STA at boot before it falls back to the AP.
    enum : uint32_t { STA_RECONNECT_TIMEOUT_MS = 10000 };

    static constexpr uint8_t MAX_LOGS = 10;
    static constexpr uint8_t LOG_ENTRY_SIZE = 128;

    char logBuffer[MAX_LOGS][LOG_ENTRY_SIZE];
    uint8_t logHead = 0;
    uint8_t logCount = 0;

    void setupOTA();

    void setupTelnet();

    void setupNTP();

    // Polled from loop(): notices the STA reconnect disablePlayerSetupAp() starts
    // and finishes what init() would otherwise have done (isWifiActive, the IP,
    // telnet) - without blocking loop() for it.
    void checkStaReconnect();

    void handleTelnet();

    void armOtaRestart();

    void notifyUpdate(FirmwareUpdateStage stage, const char *detail) const;

    void latchOtaReject(FirmwareImageCheck::Verdict verdict);

    // Moved off UPLOAD_FILE_START: a rejected upload must leave the telnet session
    // you were watching it on alive.
    void closeTelnetForOta();

public:
    // The setup AP's fixed identity - one pair, used by both AP paths
    // (enableAP, enablePlayerSetupAp) and read by the OLED discovery screen, so
    // there is no third copy to drift from the other two.
    static constexpr const char *AP_SSID = "Scoreboard";
    static constexpr const char *AP_PASSWORD = "19092026";

    void setExtraRouteRegistrar(const std::function<void(WebServer &)> &registrar) {
        extraRoutes = registrar;
    }

    void setUpdateStatusHandler(const std::function<void(FirmwareUpdateStage, const char *)> &handler) {
        updateStatusHandler = handler;
    }

    void enableAP();

    void disableAP();

    /**
     * Raise the setup AP on demand, whatever `enableDevMode` says - an explicit user
     * action, not a background service, so there is no blocking delay here.
     */
    void enablePlayerSetupAp();

    /**
     * Drops the AP and, if enableDevMode is on, starts a non-blocking STA
     * reconnect (WiFi.begin() only - no delay, no AP fallback); loop() notices the
     * connection and restarts telnet. With enableDevMode off this just leaves
     * WiFi off, same as a normal boot with it off. The port-80 server object
     * itself stays alive either way - WebServer cannot unregister handlers and
     * re-creating it would register every route twice.
     */
    void disablePlayerSetupAp();

    void init(PreferencesManager &_preferencesManager, BackDisplay &_backDisplay);

    void loop();

    void printLn(const char *message);

    void telnetFlushLogBuffer();

    bool isAnyNetworkingActive() const {
        return isAPActive || isWifiActive;
    }

    // Read by the OLED discovery screen; empty while no interface is up (same
    // string PreferencesManager itself would otherwise expose).
    const String &currentIpAddress() const {
        return preferencesManager->wifiIpAddress;
    }
};


#endif //REMOTE_DEVELOPMENT_SERVICE_H
