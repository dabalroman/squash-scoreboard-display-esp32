#ifndef REMOTE_DEVELOPMENT_SERVICE_H
#define REMOTE_DEVELOPMENT_SERVICE_H

#include <memory>
#include <WebServer.h>
#include "PreferencesManager.h"
#include "Display/BackDisplay.h"

class RemoteDevelopmentService {
    std::unique_ptr<WebServer> OTAServer;
    std::unique_ptr<WiFiServer> telnetServer;
    WiFiClient telnetClient;
    PreferencesManager *preferencesManager = nullptr;
    BackDisplay *backDisplay = nullptr;

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
    void enableAP();

    void disableAP();

    void init(PreferencesManager &_preferencesManager, BackDisplay &_backDisplay);

    void loop();

    void printLn(const char *message);

    void telnetFlushLogBuffer();

    bool isAnyNetworkingActive() const {
        return isAPActive || isWifiActive;
    }
};


#endif //REMOTE_DEVELOPMENT_SERVICE_H
