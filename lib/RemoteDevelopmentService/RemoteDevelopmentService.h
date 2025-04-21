#ifndef REMOTE_DEVELOPMENT_SERVICE_H
#define REMOTE_DEVELOPMENT_SERVICE_H

#include <deque>
#include <WebServer.h>

class RemoteDevelopmentService {
    WebServer *OTAServer = nullptr;
    WiFiServer *telnetServer = nullptr;
    WiFiClient telnetClient;

    std::deque<String> logBuffer;
    const size_t MAX_LOGS = 20;

    void setupOTA();

    void handleOTA() const;

    void setupTelnet();

    void handleTelnet();

public:
    void init();

    void handle();

    void telnetPrintLn(const char *format, ...);

    void telnetFlushLogBuffer();
};


#endif //REMOTE_DEVELOPMENT_SERVICE_H
