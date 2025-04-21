#include "RemoteDevelopmentService.h"

#include <Update.h>
#include "../../include/secrets.h"

void RemoteDevelopmentService::setupOTA() {
    OTAServer = new WebServer(80);

    OTAServer->on("/update", HTTP_POST,
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
                              telnetPrintLn(">>>>   OTA update started   <<<<");
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
}

void RemoteDevelopmentService::setupTelnet() {
    telnetServer = new WiFiServer(23);

    telnetServer->begin();
    telnetServer->setNoDelay(true);
}

void RemoteDevelopmentService::telnetPrintLn(const char *format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (telnetClient && telnetClient.connected()) {
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

void RemoteDevelopmentService::init() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFiClass::status() != WL_CONNECTED) {
        delay(500);
    }

    telnetPrintLn("Connected to %s\n", WiFi.SSID().c_str());
    telnetPrintLn("IP: %s\n", WiFi.localIP().toString().c_str());

    setupOTA();
    setupTelnet();
}

void RemoteDevelopmentService::handleTelnet() {
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

void RemoteDevelopmentService::handleOTA() const {
    this->OTAServer->handleClient();
}


void RemoteDevelopmentService::handle() {
    handleOTA();
    handleTelnet();
}
