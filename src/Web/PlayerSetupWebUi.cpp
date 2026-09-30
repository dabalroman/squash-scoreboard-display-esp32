#include "Web/PlayerSetupWebUi.h"

#include <Arduino.h>
#include <WebServer.h>
#include <string>

#include "Board.h"
#include "PlayerPalette.h"
#include "PlayerRoster.h"
#include "PreferencesManager.h"
#include "SafeRestart.h"
#include "Web/RosterJson.h"
#include "Web/RosterSaveValidator.h"
#include "Web/SettingsValidator.h"
#include "Web/WebAccessGate.h"
#include "Web/WebAssets.h"
#include "Web/WebJson.h"

namespace {
    enum : uint32_t {
        RESTART_DELAY_MS = 300,
        // A Dev-Mode brightness preview has no close() to revert it on exit (there
        // is no exit); this is the fallback that stops a forgotten tab, or a
        // dropped connection mid-drag, leaving the LEDs off the stored brightness.
        PREVIEW_SAFETY_MS = 60000
    };

    // Every plain-text reply carries Polish, so the charset is not optional - a
    // bare "text/plain" is read as latin-1 by most browsers.
    const char *const TEXT_PLAIN_PL = "text/plain; charset=utf-8";
    const char *const JSON_UTF8 = "application/json; charset=utf-8";
    const char *const ACCESS_CLOSED_NOTE =
        "Edycja jest dostępna na ekranie PROFILE albo w trybie deweloperskim w sieci Wi-Fi.";

    // Byte-for-byte what WebServer::arg() holds, embedded NULs included, so the
    // validator's length checks see what the old String-based ones did.
    class WebServerForm final : public FormLookup {
        WebServer &server;

    public:
        explicit WebServerForm(WebServer &server) : server(server) {
        }

        bool has(const char *key) const override {
            return server.hasArg(key);
        }

        std::string get(const char *key) const override {
            const String value = server.arg(key);
            return std::string(value.c_str(), value.length());
        }
    };
}

void PlayerSetupWebUi::registerRoutes(WebServer &webServer) {
    server = &webServer;

    // Ungated, whatever screen the board shows: /update and /settings' Wi-Fi form are
    // how a sealed V1 gets new firmware and credentials. The pages are static; their
    // data (/api/roster, /api/settings) is gated.
    server->on("/", HTTP_GET, page("/"));
    server->on("/settings", HTTP_GET, page("/settings"));
    server->on("/update", HTTP_GET, page("/update"));
    server->on("/app.css", HTTP_GET, asset("/app.css"));
    server->on("/profile.js", HTTP_GET, asset("/profile.js"));
    server->on("/settings.js", HTTP_GET, asset("/settings.js"));
    server->on("/update.js", HTTP_GET, asset("/update.js"));
    server->on("/api/device", HTTP_GET, [this] {
        if (active) noteActivity();
        handleDevice();
    });

    // Gated: 503 unless WebAccessGate::open() says the board is reachable - see
    // gated() and accessAllowed().
    server->on("/api/roster", HTTP_GET, gated(&PlayerSetupWebUi::handleRoster));
    server->on("/save", HTTP_POST, gated(&PlayerSetupWebUi::handleSave));
    server->on("/preview", HTTP_POST, gated(&PlayerSetupWebUi::handlePreview));
    server->on("/api/settings", HTTP_GET, gated(&PlayerSetupWebUi::handleSettings));
    server->on("/settings", HTTP_POST, gated(&PlayerSetupWebUi::handleSaveSettings));
    server->on("/settings/preview", HTTP_POST, gated(&PlayerSetupWebUi::handleSettingsPreview));

    // Ungated as a whole: the Wi-Fi half is how a sealed V1 on its fallback AP gets
    // onto a new network. The Dev Mode half checks accessAllowed() itself.
    server->on("/settings/dev", HTTP_POST, [this] {
        if (active) noteActivity();
        handleSaveDev();
    });

    // POST /update (the image itself - what v1_ota curls to) and the
    // legacy POST /connect are RemoteDevelopmentService's, registered unconditionally.
}

// The only place the access rule (WebAccessGate::open()) and its 503 live.
std::function<void()> PlayerSetupWebUi::gated(const Handler handler) {
    return [this, handler] {
        if (!accessAllowed()) {
            server->send(503, TEXT_PLAIN_PL, ACCESS_CLOSED_NOTE);
            return;
        }

        // The PROFILE idle timer exists to close the setup AP; a Dev-Mode session
        // never opened it, so it must not be touched from that path.
        if (active) noteActivity();
        (this->*handler)();
    };
}

bool PlayerSetupWebUi::accessAllowed() const {
    const bool devMode = preferencesManager.settings.enableDevMode;
    const bool sta = staConnected && staConnected();
    return WebAccessGate::open(active, devMode, sta);
}

// Browsing either page counts as using the editor; without this the mode closes
// under someone who came to /update and stayed. Only while PROFILE is actually
// open - see gated()'s comment.
std::function<void()> PlayerSetupWebUi::page(const char *path) {
    return [this, path] {
        if (active) noteActivity();
        if (!WebAssets::serveAsset(*server, path)) {
            server->send(500, TEXT_PLAIN_PL, "Brak pliku strony w firmware.");
        }
    };
}

std::function<void()> PlayerSetupWebUi::asset(const char *path) {
    return [this, path] {
        if (!WebAssets::serveAsset(*server, path)) {
            server->send(500, TEXT_PLAIN_PL, "Brak pliku strony w firmware.");
        }
    };
}

void PlayerSetupWebUi::open(const uint32_t nowMs) {
    active = true;
    lastActivity = nowMs;
    restartArmed = false;
    preview.reset();
    brightnessPreviewActive = false;
}

// Every exit (C/D, idle timeout) passes here, so an unsaved brightness preview
// never outlives the screen.
void PlayerSetupWebUi::close() {
    active = false;
    preview.reset();
    revertBrightnessPreview();
}

void PlayerSetupWebUi::noteActivity() {
    lastActivity = millis();
}

void PlayerSetupWebUi::loop() {
    if (restartArmed && static_cast<int32_t>(millis() - restartAtMs) >= 0) {
        restartArmed = false;
        safeRestart();
    }

    // Dev-Mode-only safety net (see PREVIEW_SAFETY_MS): PROFILE always reverts a
    // brightness preview through close(), so this stays out of that path - it
    // exists for the session that has no close() to call it.
    if (!active && brightnessPreviewActive
        && static_cast<int32_t>(millis() - lastPreviewMs) >= PREVIEW_SAFETY_MS) {
        revertBrightnessPreview();
    }
}

void PlayerSetupWebUi::armRestart() {
    restartAtMs = millis() + RESTART_DELAY_MS;
    restartArmed = true;
}

void PlayerSetupWebUi::reject(const char *reason) {
    server->send(400, TEXT_PLAIN_PL, reason);
}

// send_P with an explicit length writes the buffer as is - no String copy.
void PlayerSetupWebUi::sendJson(const char *json, const size_t length) {
    server->sendHeader("Cache-Control", "no-store");
    server->send_P(200, JSON_UTF8, json, length);
}

/**
 * {"max":32,"pal":[[name,"#RRGGBB"],...],"rows":[[name,colour,uid],...]}, where a
 * row's colour is a palette index for an exact preset and "#RRGGBB" otherwise.
 */
void PlayerSetupWebUi::handleRoster() {
    uint8_t paletteCount = 0;
    const PlayerPalette::PaletteEntry *palette = PlayerPalette::table(paletteCount);

    const std::vector<UserProfile *> &players = roster.profiles();
    std::vector<RosterJson::Row> rows;
    rows.reserve(players.size());
    for (size_t i = 0; i < players.size(); i++) {
        rows.push_back(RosterJson::Row{players[i]->getName(), players[i]->getColor(), players[i]->getUid()});
    }

    const std::string out = RosterJson::build(PlayerRosterLimits::MAX_PLAYERS, palette, paletteCount,
                                              rows.data(), rows.size());
    sendJson(out.data(), out.size());
}

// {"fw":"1.2.3","ssid":"...","hw":"V1 ESP32-S3"} - ungated: the /update page needs it on a
// sealed V1, and hw tells which board a .bin must be built for (V1 refuses a V2 image).
void PlayerSetupWebUi::handleDevice() {
    std::string out = "{\"fw\":";
    WebJson::appendString(out, FW_VERSION);
    out += ",\"ssid\":";
    WebJson::appendString(out, preferencesManager.settings.wifiSSID);
    out += ",\"hw\":";
    WebJson::appendString(out, Board::NAME);
    out += '}';
    sendJson(out.data(), out.size());
}

/**
 * Server-authoritative and atomic: the whole sizeof(PlayersData) blob is staged in
 * RAM and validated before a single putBytes. Any rejection leaves NVS untouched.
 */
void PlayerSetupWebUi::handleSave() {
    if (server->hasArg("reset") && server->arg("reset") == "1") {
        if (!roster.resetToDefaults()) {
            server->send(500, TEXT_PLAIN_PL, "Nie udało się zapisać w NVS.");
            return;
        }

        server->send(200, TEXT_PLAIN_PL, "Przywrócono profile fabryczne. Restart...");
        armRestart();
        return;
    }

    PlayersData staged;
    const char *error = RosterSaveValidator::validate(WebServerForm(*server), &PlayerRoster::generateUid, staged);
    if (error != nullptr) {
        reject(error);
        return;
    }

    if (!roster.save(staged)) {
        server->send(500, TEXT_PLAIN_PL, "Nie udało się zapisać w NVS.");
        return;
    }

    server->send(200, TEXT_PLAIN_PL, "Zapisano. Restart...");
    armRestart();
}

/**
 * State only - never NVS, never a draw. Gated like /save: the preview must not
 * work with the AP closed, since then nobody has the roster editor open.
 */
void PlayerSetupWebUi::handlePreview() {
    if (!server->hasArg("playerId") || !server->hasArg("color")) {
        reject("Brak pola playerId lub color.");
        return;
    }

    const String idArg = server->arg("playerId");
    if (idArg.length() < 1 || idArg.length() > 2) {
        reject("Błędny playerId.");
        return;
    }

    for (size_t c = 0; c < idArg.length(); c++) {
        if (idArg[c] < '0' || idArg[c] > '9') {
            reject("Błędny playerId.");
            return;
        }
    }

    const long playerId = idArg.toInt();
    if (playerId < 0 || playerId >= PlayerRosterLimits::MAX_PLAYERS) {
        reject("Błędny playerId.");
        return;
    }

    Color color;
    if (!PlayerPalette::fromHex(server->arg("color"), color)) {
        reject("Błędny kolor.");
        return;
    }

    // The first preview since open() pairs the player with the lowest other
    // roster id, in that player's stored colour.
    PreviewSlot partner;
    const std::vector<UserProfile *> &players = roster.profiles();
    for (size_t i = 0; i < players.size(); i++) {
        if (players[i]->getId() != playerId) {
            partner = PreviewSlot{players[i]->getId(), players[i]->getColor(), true};
            break;
        }
    }

    preview.show(static_cast<uint8_t>(playerId), color, partner);

    server->send(204, TEXT_PLAIN_PL, "");
}

// {"level":1-8,"buzzer":0|1,"devMode":0|1} - the stored values, never a preview.
void PlayerSetupWebUi::handleSettings() {
    const PrefsData &settings = preferencesManager.settings;
    char out[48];
    const int length = snprintf(out, sizeof(out), "{\"level\":%u,\"buzzer\":%u,\"devMode\":%u}",
                                static_cast<unsigned>(PrefsBrightness::byteToLevel(settings.brightness)),
                                static_cast<unsigned>(settings.enableBuzzer ? 1 : 0),
                                static_cast<unsigned>(settings.enableDevMode ? 1 : 0));
    sendJson(out, static_cast<size_t>(length));
}

/**
 * No restart: save() applies brightness and buzzer live, and Dev Mode is read when
 * PROFILE closes (disablePlayerSetupAp). A rejection leaves RAM and NVS untouched.
 */
void PlayerSetupWebUi::handleSaveSettings() {
    const char *error = SettingsValidator::stage(WebServerForm(*server), preferencesManager.settings);
    if (error != nullptr) {
        reject(error);
        return;
    }

    preferencesManager.save();
    // save() has just applied the stored brightness over any preview.
    brightnessPreviewActive = false;
    redrawRequested = true;

    server->send(200, TEXT_PLAIN_PL, "Zapisano.");
}

/**
 * Wi-Fi credentials, and Dev Mode when the gate is open. Both only take effect at
 * boot, so a success always restarts - deferred, like /save, never a delay() here.
 */
void PlayerSetupWebUi::handleSaveDev() {
    const char *error = SettingsValidator::stageDev(WebServerForm(*server), accessAllowed(),
                                                    preferencesManager.settings);
    if (error != nullptr) {
        server->send(error == SettingsValidator::DEV_MODE_LOCKED ? 403 : 400, TEXT_PLAIN_PL, error);
        return;
    }

    preferencesManager.save();
    brightnessPreviewActive = false;
    redrawRequested = true;

    server->send(200, TEXT_PLAIN_PL, "Zapisano. Restart...");
    armRestart();
}

// LEDs only - never save(), so a preview never reaches NVS or the apply choke point.
void PlayerSetupWebUi::handleSettingsPreview() {
    bool cancel = false;
    uint8_t brightness = 0;
    const char *error = SettingsValidator::validatePreview(WebServerForm(*server), cancel, brightness);
    if (error != nullptr) {
        reject(error);
        return;
    }

    if (cancel) {
        revertBrightnessPreview();
    } else {
        previewBrightness(brightness);
        brightnessPreviewActive = true;
        lastPreviewMs = millis();
    }
    redrawRequested = true;

    server->send(204, TEXT_PLAIN_PL, "");
}

void PlayerSetupWebUi::revertBrightnessPreview() {
    if (!brightnessPreviewActive) {
        return;
    }

    brightnessPreviewActive = false;
    preferencesManager.apply();
}
