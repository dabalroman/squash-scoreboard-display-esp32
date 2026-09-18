#ifndef REMOTE_DEVELOPMENT_SERVICE_H
#define REMOTE_DEVELOPMENT_SERVICE_H

#include <functional>
#include <memory>
#include <WiFi.h>
#include <WebServer.h>
#include "PreferencesManager.h"
#include "Display/BackDisplay.h"

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

    bool isAPActive = false;
    bool isWifiActive = false;
    bool isTelnetActive = false;
    bool isOTAActive = false;
    bool isNTPActive = false;

    static constexpr uint8_t MAX_LOGS = 10;
    static constexpr uint8_t LOG_ENTRY_SIZE = 128;

    char logBuffer[MAX_LOGS][LOG_ENTRY_SIZE];
    uint8_t logHead = 0;
    uint8_t logCount = 0;

    void setupOTA();

    void setupTelnet();

    void setupNTP();

    void handleTelnet();

public:
    void setExtraRouteRegistrar(const std::function<void(WebServer &)> &registrar) {
        extraRoutes = registrar;
    }

    void enableAP();

    void disableAP();

    /**
     * Raise the setup AP on demand, whatever `enableWifi` says - an explicit user
     * action, not a background service, so there is no blocking delay here.
     *
     * STA is torn down and does not come back until the next reboot; the roster
     * editor always ends in one on save, and the log says so on cancel.
     */
    void enablePlayerSetupAp();

    // Drops the AP only. The port-80 server object stays alive, because WebServer
    // cannot unregister handlers and re-creating it would register them twice.
    void disablePlayerSetupAp();

    void init(PreferencesManager &_preferencesManager, BackDisplay &_backDisplay);

    void loop();

    void printLn(const char *message);

    void telnetFlushLogBuffer();

    bool isAnyNetworkingActive() const {
        return isAPActive || isWifiActive;
    }
};


#endif //REMOTE_DEVELOPMENT_SERVICE_H
