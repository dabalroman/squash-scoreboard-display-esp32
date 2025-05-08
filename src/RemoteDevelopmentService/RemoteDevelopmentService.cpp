#include "RemoteDevelopmentService.h"

#include <Update.h>
#include <Adafruit_SSD1306.h>

#include "../../src/PreferencesManager.h"

void RemoteDevelopmentService::setupOTA() {
    OTAServer = new WebServer(80);

    OTAServer->on("/", HTTP_GET, [this] {
        const String html = "<html><body><form action=\"/connect\" method=\"POST\">"
                "SSID:<br><input type=\"text\" name=\"ssid\"><br>"
                "Password:<br><input type=\"text\" name=\"password\"><br><br>"
                "<input type=\"submit\" value=\"Connect\">"
                "</form></body></html>";
        OTAServer->send(200, "text/html", html);
    });

    OTAServer->on("/connect", HTTP_POST, [this] {
        if (OTAServer->hasArg("ssid") && OTAServer->hasArg("password")) {
            const String newSSID = OTAServer->arg("ssid");
            const String newPassword = OTAServer->arg("password");

            printLn("New data: '%s' '%s'", newSSID.c_str(), newPassword.c_str());

            strncpy(preferencesManager->settings.wifiSSID, newSSID.c_str(), sizeof(preferencesManager->settings.wifiSSID));
            preferencesManager->settings.wifiSSID[sizeof(preferencesManager->settings.wifiSSID) - 1] = '\0';

            strncpy(preferencesManager->settings.wifiPassword, newPassword.c_str(), sizeof(preferencesManager->settings.wifiPassword));
            preferencesManager->settings.wifiPassword[sizeof(preferencesManager->settings.wifiPassword) - 1] = '\0';

            preferencesManager->save();

            printLn("SAVED");

            display->clearDisplay();
            display->setCursor(0, 0);
            display->println("Credentials saved! Rebooting...");
            display->display();

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

    isOTAEnabled = true;
}

void RemoteDevelopmentService::setupTelnet() {
    if (WiFiClass::status() != WL_CONNECTED) {
        return;
    }

    telnetServer = new WiFiServer(23);

    telnetServer->begin();
    telnetServer->setNoDelay(true);

    isTelnetEnabled = true;
}

void RemoteDevelopmentService::setupNTP() {
    if (WiFiClass::status() != WL_CONNECTED) {
        return;
    }

    configTime(3600, 3600, "pool.ntp.org");
    tm timeInfo;
    getLocalTime(&timeInfo);

    isNTPEnabled = true;
}

void RemoteDevelopmentService::printLn(const char *format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (WiFiClass::status() == WL_CONNECTED && telnetClient && telnetClient.connected()) {
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

void RemoteDevelopmentService::init(PreferencesManager &_preferencesManager, Adafruit_SSD1306 &screen) {
    preferencesManager = &_preferencesManager;
    display = &screen;

    const String savedSSID = preferencesManager->settings.wifiSSID;
    const String savedPassword = preferencesManager->settings.wifiPassword;
    printLn("Read: '%s' '%s'", savedSSID.c_str(), savedPassword.c_str());

    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    const unsigned long startAttemptTime = millis();
    constexpr unsigned long timeout = 10000;

    display->clearDisplay();
    display->println("Connecting to " + savedSSID);
    display->println("Using " + savedPassword);
    display->display();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
        delay(500);
    }

    if (WiFiClass::status() != WL_CONNECTED) {
        enableAP();
    } else {
        display->clearDisplay();
        display->setCursor(0, 0);
        display->println("Connected to " + WiFi.SSID());
        display->println(String("IP: ") + WiFi.localIP().toString());
        display->display();

        isWifiConnected = true;
    }

    delay(1000);

    setupOTA();
    setupTelnet();
    // setupNTP();
}

void RemoteDevelopmentService::enableAP() {
    WiFi.softAP("ESP32_Setup", "12345678");

    display->clearDisplay();
    display->setCursor(0, 0);
    display->println(F("AP MODE"));
    display->println(F("SSID: ESP32_Setup"));
    display->println(F("Password: 12345678"));
    display->println("IP address: " + WiFi.softAPIP().toString());
    display->display();

    isAPEnabled = true;
}

void RemoteDevelopmentService::disableAP() {
    WiFi.softAPdisconnect();
    isAPEnabled = false;
}

void RemoteDevelopmentService::handleTelnet() {
    if (WiFiClass::status() != WL_CONNECTED) {
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
    this->OTAServer->handleClient();
    handleTelnet();
}
