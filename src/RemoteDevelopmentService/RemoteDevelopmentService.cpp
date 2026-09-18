#include "RemoteDevelopmentService.h"

#include <Update.h>

#include "LoggerHelper.h"
#include "Strings.h"
#include "SafeRestart.h"
#include "Utils.h"
#include "Display/BackDisplay.h"

void RemoteDevelopmentService::setupOTA() {
    if (!isAnyNetworkingActive()) {
        return;
    }

    // No longer once per boot: the roster editor raises its AP on demand and calls
    // this again. WebServer::on() does not deduplicate and there is no
    // removeHandler, so a second pass would register every route twice.
    if (OTAServer) {
        return;
    }

    OTAServer = std::make_unique<WebServer>(80);

    OTAServer->on("/connect", HTTP_POST, [this] {
        if (OTAServer->hasArg("ssid") && OTAServer->hasArg("password")) {
            const String newSSID = OTAServer->arg("ssid");
            const String newPassword = OTAServer->arg("password");

            strncpy(preferencesManager->settings.wifiSSID, newSSID.c_str(),
                    sizeof(preferencesManager->settings.wifiSSID));
            preferencesManager->settings.wifiSSID[sizeof(preferencesManager->settings.wifiSSID) - 1] = '\0';

            strncpy(preferencesManager->settings.wifiPassword, newPassword.c_str(),
                    sizeof(preferencesManager->settings.wifiPassword));
            preferencesManager->settings.wifiPassword[sizeof(preferencesManager->settings.wifiPassword) - 1] = '\0';

            preferencesManager->save();

            backDisplay->clear();
            backDisplay->setCursorToLine();
            backDisplay->print(Str::BOOT_OLED_CREDENTIALS_SAVED);
            backDisplay->display();

            OTAServer->send(200, "text/html", "Credentials saved! Rebooting...");
            delay(1000);
            safeRestart();
        } else {
            OTAServer->send(400, "text/html", "Missing SSID or Password");
        }
    });

    OTAServer->on(
        "/update",
        HTTP_POST,
        [this] {
            // OTA - onUploadEnd. The whole body has been drained by now, whatever the
            // upload handler did with it, so a rejection can still answer properly.
            ::printLn("OTA: %u bytes, chip 0x%04X, forced %d, %s",
                    static_cast<unsigned>(otaBytes), otaHeader.seenChipId(), otaForced ? 1 : 0,
                    otaRejectReason != nullptr
                        ? "rejected"
                        : (Update.hasError() ? "write error" : "accepted"));

            OTAServer->sendHeader("Connection", "close");

            if (otaRejectReason != nullptr) {
                // 400, and no restart: the running firmware keeps going, which is the
                // whole point of checking before Update.end(true) switches partitions.
                OTAServer->send(400, "text/plain; charset=utf-8", otaRejectReason);
                notifyUpdate(FirmwareUpdateStage::Failed, otaRejectLabel);
                return;
            }

            if (Update.hasError()) {
                // Say what went wrong, not "FAIL": the scripted path
                // (`pio run -t upload -e lolin_s2_mini_ota`) curls this and used to be
                // handed HTTP 200 for a flash that never landed.
                OTAServer->send(400, "text/plain; charset=utf-8", Update.errorString());
                notifyUpdate(FirmwareUpdateStage::Failed, Str::OTA_EINK_TITLE_ERROR);
                return;
            }

            OTAServer->send(200, "text/plain; charset=utf-8", "OK");
            notifyUpdate(FirmwareUpdateStage::Succeeded, nullptr);
            armOtaRestart();
        },
        [this] {
            // OTA - onUpload
            HTTPUpload &upload = OTAServer->upload();

            if (upload.status == UPLOAD_FILE_START) {
                otaHeader.reset();
                otaRejectReason = nullptr;
                otaRejectLabel = nullptr;
                otaBytes = 0;
                otaVerdictLogged = false;
                otaTelnetClosed = false;
                // From the query string, not a form field: WebServer parses the URL
                // arguments before the multipart body and merges them into the POST
                // arguments, so this is readable both here and in the end handler.
                otaForced = OTAServer->hasArg("force") && OTAServer->arg("force") == "1";

                ::printLn("OTA: upload started from %s, file %s, forced %d",
                        OTAServer->client().remoteIP().toString().c_str(),
                        upload.filename.c_str(), otaForced ? 1 : 0);

                notifyUpdate(FirmwareUpdateStage::Started, nullptr);

                Update.begin(UPDATE_SIZE_UNKNOWN);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                otaBytes += upload.currentSize;
                otaHeader.feed(upload.buf, upload.currentSize);

                if (!otaVerdictLogged && otaHeader.ready()) {
                    otaVerdictLogged = true;
                    latchOtaReject(otaHeader.verdict());
                }

                if (otaRejectReason != nullptr) {
                    // Nothing more reaches flash. The body still drains on its own -
                    // WebServer's boundary search reads it regardless of what happens
                    // here - so the end handler still runs and can send the 400.
                    return;
                }

                closeTelnetForOta();
                Update.write(upload.buf, upload.currentSize);
            } else if (upload.status == UPLOAD_FILE_END) {
                // A file too short to hold a header never reached a verdict above.
                if (otaRejectReason == nullptr && !otaHeader.ready()) {
                    latchOtaReject(FirmwareImageCheck::Verdict::NotFirmware);
                }

                if (otaRejectReason != nullptr) {
                    // abort() on its own is enough: it resets the writer and latches an
                    // error, after which end(true) could only return false.
                    Update.abort();
                } else {
                    Update.end(true);
                }
            } else if (upload.status == UPLOAD_FILE_ABORTED) {
                // Only the dropped-connection case, and the end handler never runs for
                // it, so nothing is sent back here.
                Update.abort();
                otaRejectReason = nullptr;
                otaRejectLabel = nullptr;
                ::printLn("OTA: upload aborted after %u bytes", static_cast<unsigned>(otaBytes));
            }
        }
    );

    if (extraRoutes) {
        extraRoutes(*OTAServer);
    }

    // Registered LAST, and that ordering is the whole design: WebServer dispatches
    // to the first handler that matches, so on V2 the web UI's own "/" (registered
    // just above) wins and this never runs. On V1 there is no web UI at all, and
    // this stays the only way to type WiFi credentials into a board whose stored
    // ones are wrong - which matters more there, because V1 is flashed over OTA.
    // A fallback by ordering, rather than by `#if BOARD_REV`, which belongs in
    // Board.h and the hardware wrappers only.
    OTAServer->on("/", HTTP_GET, [this] {
        const String html = "<html><body><h1>Squash Scoreboard Display</h1><form action=\"/connect\" method=\"POST\">"
                "SSID:<br><input type=\"text\" name=\"ssid\"><br>"
                "Password:<br><input type=\"password\" name=\"password\"><br><br>"
                "<input type=\"submit\" value=\"Connect\">"
                "</form></body></html>";
        OTAServer->send(200, "text/html", html);
    });

    OTAServer->begin();

    isOTAActive = true;
}

void RemoteDevelopmentService::setupTelnet() {
    if (!isWifiActive) {
        return;
    }

    telnetServer = std::make_unique<WiFiServer>(23);

    telnetServer->begin();
    telnetServer->setNoDelay(true);

    isTelnetActive = true;
}

void RemoteDevelopmentService::setupNTP() {
    if (!isWifiActive) {
        return;
    }

    configTime(3600, 3600, "pool.ntp.org");  // async
    isNTPActive = true;
}

void RemoteDevelopmentService::printLn(const char *message) {
    if (isWifiActive && telnetClient && telnetClient.connected()) {
        telnetClient.println(message);
    } else {
        strncpy(logBuffer[logHead], message, LOG_ENTRY_SIZE - 1);
        logBuffer[logHead][LOG_ENTRY_SIZE - 1] = '\0';
        logHead = (logHead + 1) % MAX_LOGS;
        if (logCount < MAX_LOGS) logCount++;
    }
}

void RemoteDevelopmentService::telnetFlushLogBuffer() {
    uint8_t idx = (logHead + MAX_LOGS - logCount) % MAX_LOGS;
    while (logCount > 0) {
        telnetClient.println(logBuffer[idx]);
        idx = (idx + 1) % MAX_LOGS;
        logCount--;
    }
}

void RemoteDevelopmentService::init(PreferencesManager &_preferencesManager, BackDisplay &_backDisplay) {
    preferencesManager = &_preferencesManager;
    backDisplay = &_backDisplay;

    const String savedSSID = preferencesManager->settings.wifiSSID;
    const String savedPassword = preferencesManager->settings.wifiPassword;

    if (!preferencesManager->settings.enableDevMode) {
        preferencesManager->wifiIpAddress = "";
        return;
    }

    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    const unsigned long startAttemptTime = millis();
    constexpr unsigned long timeout = 10000;

    backDisplay->clear();
    backDisplay->setCursorToLine();
    backDisplay->print(Str::BOOT_OLED_WIFI_CONNECTING_PREFIX + savedSSID);
    backDisplay->display();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
        delay(500);
    }

    if (WiFi.status() != WL_CONNECTED) {
        enableAP();
    } else {
        backDisplay->clear();
        backDisplay->setCursorToLine();
        backDisplay->print(WiFi.SSID());
        backDisplay->print(WiFi.localIP().toString());
        backDisplay->display();

        preferencesManager->wifiIpAddress = WiFi.localIP().toString();

        isWifiActive = true;
    }

    setupOTA();
    setupTelnet();
}

void RemoteDevelopmentService::enableAP() {
    WiFi.softAP("Scoreboard", "19092026");

    preferencesManager->wifiIpAddress = WiFi.softAPIP().toString();

    backDisplay->clear();
    backDisplay->setCursorToLine();
    backDisplay->println(F("Scoreboard"));
    backDisplay->println(F("19092026"));
    backDisplay->println(WiFi.softAPIP().toString());
    backDisplay->display();

    delay(5000);

    isAPActive = true;
}

void RemoteDevelopmentService::disableAP() {
    WiFi.softAPdisconnect();
    preferencesManager->wifiIpAddress = "";
    isAPActive = false;
}

void RemoteDevelopmentService::enablePlayerSetupAp() {
    // Clear the flag before tearing STA down: handleTelnet() would otherwise poll a
    // server whose socket has just gone away.
    isWifiActive = false;
    isTelnetActive = false;

    if (telnetClient) {
        telnetClient.stop();
    }
    if (telnetServer) {
        telnetServer->close();
    }

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Scoreboard", "19092026");

    preferencesManager->wifiIpAddress = WiFi.softAPIP().toString();
    isAPActive = true;

    // Load-bearing: with enableDevMode off (the default on a fresh device) init()
    // returned early and no WebServer exists yet.
    setupOTA();
}

void RemoteDevelopmentService::disablePlayerSetupAp() {
    WiFi.softAPdisconnect(true);
    preferencesManager->wifiIpAddress = "";
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
    if (isOTAActive) {
        OTAServer->handleClient();
    }

    // After handleClient(), so the reply is already queued when the board goes down.
    if (otaRestartArmed && static_cast<int32_t>(millis() - otaRestartAtMs) >= 0) {
        otaRestartArmed = false;
        safeRestart();
    }

    handleTelnet();
}

void RemoteDevelopmentService::armOtaRestart() {
    otaRestartAtMs = millis() + OTA_RESTART_DELAY_MS;
    otaRestartArmed = true;
}

void RemoteDevelopmentService::notifyUpdate(const FirmwareUpdateStage stage, const char *detail) const {
    if (updateStatusHandler) {
        updateStatusHandler(stage, detail);
    }
}

/**
 * Turns a verdict into the rejection, or into a log line when it was forced past.
 * The check runs either way - forcing only means it does not abort - so the log
 * always records what the image actually was.
 */
void RemoteDevelopmentService::latchOtaReject(const FirmwareImageCheck::Verdict verdict) {
    if (verdict == FirmwareImageCheck::Verdict::Ok) {
        ::printLn("OTA: header ok, chip 0x%04X", otaHeader.seenChipId());
        return;
    }

    const char *label = verdict == FirmwareImageCheck::Verdict::WrongChip
                            ? Str::OTA_EINK_TITLE_WRONG_BOARD
                            : Str::OTA_EINK_TITLE_BAD_FILE;

    if (otaForced) {
        ::printLn("OTA: header rejected (%s, chip 0x%04X) but forced - writing anyway",
                label, otaHeader.seenChipId());
        return;
    }

    ::printLn("OTA: header rejected - %s, chip 0x%04X", label, otaHeader.seenChipId());
    otaRejectReason = FirmwareImageCheck::reasonFor(verdict);
    otaRejectLabel = label;
}

void RemoteDevelopmentService::closeTelnetForOta() {
    if (otaTelnetClosed) {
        return;
    }
    otaTelnetClosed = true;

    if (telnetClient && telnetClient.connected()) {
        telnetFlushLogBuffer();
        telnetClient.stop();
    }
    if (telnetServer) {
        telnetServer->close();
    }
}
