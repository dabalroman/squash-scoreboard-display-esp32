#ifndef REMOTE_DEVELOPMENT_SERVICE_H
#define REMOTE_DEVELOPMENT_SERVICE_H

#include <deque>
#include <WebServer.h>
#include <Adafruit_SSD1306.h>
#include "../PreferencesManager.h"
#include "Display/BackDisplay.h"

class RemoteDevelopmentService {
    WebServer *OTAServer = nullptr;
    WiFiServer *telnetServer = nullptr;
    WiFiClient telnetClient;
    PreferencesManager *preferencesManager = nullptr;
    BackDisplay *backDisplay = nullptr;

    bool isAPEnabled = false;
    bool isWifiConnected = false;
    bool isTelnetEnabled = false;
    bool isOTAEnabled = false;
    bool isNTPEnabled = false;

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

    bool getIsAPEnabled() const {
        return isAPEnabled;
    }

    bool getIsWifiConnected() const {
        return isWifiConnected;
    }
};


#endif //REMOTE_DEVELOPMENT_SERVICE_H
