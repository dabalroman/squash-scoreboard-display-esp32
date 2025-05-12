#include "RemoteDevelopmentService.h"

#include <Update.h>

#include "../../src/PreferencesManager.h"
#include "Display/BackDisplay.h"

void RemoteDevelopmentService::setupOTA() {
    if (!isAnyNetworkingActive()) {
        return;
    }

    OTAServer = new WebServer(80);

    OTAServer->on("/", HTTP_GET, [this] {
        const String html = "<html><body><h1>Squash Scoreboard Display</h1><form action=\"/connect\" method=\"POST\">"
                "SSID:<br><input type=\"text\" name=\"ssid\"><br>"
                "Password:<br><input type=\"password\" name=\"password\"><br><br>"
                "<input type=\"submit\" value=\"Connect\">"
                "</form></body></html>";
        OTAServer->send(200, "text/html", html);
    });

    OTAServer->on("/connect", HTTP_POST, [this] {
        if (OTAServer->hasArg("ssid") && OTAServer->hasArg("password")) {
            const String newSSID = OTAServer->arg("ssid");
            const String newPassword = OTAServer->arg("password");

            printLn("New data: '%s' '%s'", newSSID.c_str(), newPassword.c_str());

            strncpy(preferencesManager->settings.wifiSSID, newSSID.c_str(),
                    sizeof(preferencesManager->settings.wifiSSID));
            preferencesManager->settings.wifiSSID[sizeof(preferencesManager->settings.wifiSSID) - 1] = '\0';

            strncpy(preferencesManager->settings.wifiPassword, newPassword.c_str(),
                    sizeof(preferencesManager->settings.wifiPassword));
            preferencesManager->settings.wifiPassword[sizeof(preferencesManager->settings.wifiPassword) - 1] = '\0';

            preferencesManager->save();

            printLn("SAVED");

            backDisplay->clear();
            backDisplay->setCursorToLine();
            backDisplay->print("Credentials saved! Rebooting...");
            backDisplay->display();

            OTAServer->send(200, "text/html", "Credentials saved! Rebooting...");
            delay(1000);
            ESP.restart();
        } else {
            OTAServer->send(400, "text/html", "Missing SSID or Password");
        }
    });

    OTAServer->on(
        "/update",
        HTTP_POST,
        [this] {
            // OTA - onUploadEnd
            OTAServer->sendHeader("Connection", "close");
            OTAServer->send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
            ESP.restart();
        },
        [this] {
            // OTA - onUpload
            HTTPUpload &upload = OTAServer->upload();
            if (upload.status == UPLOAD_FILE_START) {
                if (telnetClient && telnetClient.connected()) {
                    telnetFlushLogBuffer();
                    printLn(">>>>   OTA update started   <<<<");
                    telnetClient.stop();
                    telnetServer->close();
                }

                Update.begin(UPDATE_SIZE_UNKNOWN);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                Update.write(upload.buf, upload.currentSize);
            } else if (upload.status == UPLOAD_FILE_END) {
                Update.end(true);
            }
        }
    );

    OTAServer->begin();

    isOTAActive = true;
}

void RemoteDevelopmentService::setupTelnet() {
    if (!isWifiActive) {
        return;
    }

    telnetServer = new WiFiServer(23);

    telnetServer->begin();
    telnetServer->setNoDelay(true);

    isTelnetActive = true;
}

void RemoteDevelopmentService::setupNTP() {
    if (!isWifiActive) {
        return;
    }

    configTime(3600, 3600, "pool.ntp.org");
    tm timeInfo;
    getLocalTime(&timeInfo);

    isNTPActive = true;
}

void RemoteDevelopmentService::printLn(const char *format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (isWifiActive && telnetClient && telnetClient.connected()) {
        telnetClient.println(buf);
    } else {
        if (logBuffer.size() >= MAX_LOGS) {
            logBuffer.pop_front();
        }
        logBuffer.emplace_back(buf);
    }
}

void RemoteDevelopmentService::telnetFlushLogBuffer() {
    while (!logBuffer.empty()) {
        telnetClient.println(logBuffer.front());
        logBuffer.pop_front();
    }
}

void RemoteDevelopmentService::init(PreferencesManager &_preferencesManager, BackDisplay &_backDisplay) {
    preferencesManager = &_preferencesManager;
    backDisplay = &_backDisplay;

    const String savedSSID = preferencesManager->settings.wifiSSID;
    const String savedPassword = preferencesManager->settings.wifiPassword;

    if (!preferencesManager->settings.enableWifi) {
        return;
    }

    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    const unsigned long startAttemptTime = millis();
    constexpr unsigned long timeout = 10000;

    backDisplay->clear();
    backDisplay->setCursorToLine();
    backDisplay->print("Connecting to " + savedSSID);
    backDisplay->print("Using " + savedPassword);
    backDisplay->display();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
        delay(500);
    }

    if (WiFiClass::status() != WL_CONNECTED) {
        enableAP();
    } else {
        backDisplay->clear();
        backDisplay->setCursorToLine();
        backDisplay->print(WiFi.SSID());
        backDisplay->print(WiFi.localIP().toString());
        backDisplay->display();

        isWifiActive = true;
    }

    setupOTA();
    setupTelnet();
}

void RemoteDevelopmentService::enableAP() {
    if (!preferencesManager->settings.enableAp) {
        return;
    }

    WiFi.softAP("SquashCounter", "12345678");

    backDisplay->clear();
    backDisplay->setCursorToLine();
    backDisplay->println(F("SquashCount"));
    backDisplay->println(F("12345678"));
    backDisplay->println(WiFi.softAPIP().toString());
    backDisplay->display();

    delay(5000);

    isAPActive = true;
}

void RemoteDevelopmentService::disableAP() {
    WiFi.softAPdisconnect();
    isAPActive = false;
}

void RemoteDevelopmentService::handleTelnet() {
    if (!isWifiActive) {
        return;
    }

    if (telnetServer->hasClient()) {
        if (!telnetClient || !telnetClient.connected()) {
            telnetClient = telnetServer->available();
            telnetFlushLogBuffer();
        } else {
            WiFiClient newClient = telnetServer->available();
            newClient.stop();
        }
    }
}

void RemoteDevelopmentService::loop() {
    if (!isAnyNetworkingActive()) {
        return;
    }

    this->OTAServer->handleClient();
    handleTelnet();
}
