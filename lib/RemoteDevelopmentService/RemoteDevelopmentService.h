#ifndef REMOTE_DEVELOPMENT_SERVICE_H
#define REMOTE_DEVELOPMENT_SERVICE_H

#include <deque>
#include <WebServer.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

class RemoteDevelopmentService {
    WebServer *OTAServer = nullptr;
    WiFiServer *telnetServer = nullptr;
    WiFiClient telnetClient;
    Preferences &preferences;

    std::deque<String> logBuffer;
    const size_t MAX_LOGS = 20;

    void setupOTA(Adafruit_SSD1306 &display);

    void setupTelnet();

    void handleTelnet();

public:
    explicit RemoteDevelopmentService(Preferences &preferences) : preferences(preferences) {};

    void init(Adafruit_SSD1306 &display);

    void loop();

    void printLn(const char *format, ...);

    void telnetFlushLogBuffer();
};


#endif //REMOTE_DEVELOPMENT_SERVICE_H
