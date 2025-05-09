#ifndef REMOTE_DEVELOPMENT_SERVICE_H
#define REMOTE_DEVELOPMENT_SERVICE_H

#include <deque>
#include <WebServer.h>
#include "../PreferencesManager.h"
#include "Display/BackDisplay.h"

class RemoteDevelopmentService {
    WebServer *OTAServer = nullptr;
    WiFiServer *telnetServer = nullptr;
    WiFiClient telnetClient;
    PreferencesManager *preferencesManager = nullptr;
    BackDisplay *backDisplay = nullptr;

    bool isAPActive = false;
    bool isWifiActive = false;
    bool isTelnetActive = false;
    bool isOTAActive = false;
    bool isNTPActive = false;

    std::deque<String> logBuffer;
    const size_t MAX_LOGS = 20;

    void setupOTA();

    void setupTelnet();

    void setupNTP();

    void handleTelnet();

public:
    void enableAP();

    void disableAP();

    void init(PreferencesManager &_preferencesManager, BackDisplay &_backDisplay);

    void loop();

    void printLn(const char *format, ...);

    void telnetFlushLogBuffer();

    bool isAnyNetworkingActive() const {
        return isAPActive || isWifiActive;
    }
};


#endif //REMOTE_DEVELOPMENT_SERVICE_H
