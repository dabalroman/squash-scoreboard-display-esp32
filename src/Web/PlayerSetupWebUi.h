#ifndef PLAYER_SETUP_WEB_UI_H
#define PLAYER_SETUP_WEB_UI_H

/**
 * The device's web UI, served over the setup AP. One implementation on both
 * boards - no `#if BOARD_REV` - because the page itself is ~33 KB, nowhere near
 * V1's 1280 KB app slot; the only thing V1 lacks is the e-paper QR placard, and
 * that already has its own hardware-wrapper stub (EInkDisplay).
 *
 * Two screens, sharing one shell and nav bar:
 *   GET /        the profile (roster) editor - the default screen
 *   POST /save   its save endpoint
 *   POST /preview  live LED colour preview while a dialog is open - state only,
 *                   never NVS, never a draw; see the "LED preview" section below
 *   GET /update  the firmware upload screen, which posts to POST /update
 *
 * `/` used to be the WiFi credentials form. That form now lives on the update
 * screen, because WiFi exists here only to serve OTA: the profile editor always
 * runs on the AP, never on the house network, so joining it is always the AP's
 * fixed name and password - shown on the OLED on both boards, and additionally
 * as two QR codes on V2's e-paper placard - never an IP address nobody can guess.
 *
 * `POST /connect` (saving the credentials) and `POST /update` (writing the image)
 * both still belong to RemoteDevelopmentService - the latter is what
 * `lolin_s2_mini_ota` curls a multipart body to - so only GET pages are added here.
 *
 * LANGUAGE: Polish only, and deliberately not routed through src/Strings.h. The
 * device is Polish, the page is rendered by a phone browser rather than by a GFX
 * font, so unlike every screen string it can carry real diacritics - and a second
 * language would double a payload that already dominates this file. This file is
 * UTF-8, like the few other sources here that already carry non-ASCII characters.
 *
 * The profile *names* the page accepts stay printable ASCII, because those do
 * reach the LED, OLED and e-paper fonts, none of which has an accented glyph.
 *
 * Ownership, and why no route lambda may capture a view: WebServer (core 2.0.17)
 * has on()/addHandler() but no removeHandler, so a handler lives as long as the
 * server object, while changeDeviceMode() destroys the outgoing mode from inside
 * a lambda that mode owns. This object therefore lives at file scope in main.cpp
 * and its handlers capture only `this`.
 *
 * Handlers run synchronously from RemoteDevelopmentService::loop(), on the
 * Arduino loop task, so there is no locking. That loop runs before the 50 ms
 * frame gate and during overlays, which is why the `active` gate is required and
 * not cosmetic: a request arriving after the screen closed must be refused.
 */

#include <Arduino.h>
#include <WebServer.h>

#include "PlayerPalette.h"
#include "PlayerRoster.h"
#include "PreferencesManager.h"
#include "SafeRestart.h"

class PlayerSetupWebUi {
public:
    // One side of the LED preview pair. Public so PlayerSetupView can read it;
    // the array itself stays private, reached only through previewSlot().
    // Constructors, not member initialisers: under gnu++11 those make it a
    // non-aggregate and PreviewSlot{id, color, used} would not compile.
    struct PreviewSlot {
        uint8_t id;
        Color color;
        bool used;

        PreviewSlot() : id(0), color(), used(false) {
        }

        PreviewSlot(const uint8_t id, const Color color, const bool used) : id(id), color(color), used(used) {
        }
    };

    PlayerSetupWebUi(PlayerRoster &roster, PreferencesManager &preferencesManager)
        : roster(roster), preferencesManager(preferencesManager) {
    }

    // Called once per WebServer object, from RemoteDevelopmentService::setupOTA().
    void registerRoutes(WebServer &webServer) {
        server = &webServer;

        server->on("/", HTTP_GET, [this] { handleRoot(); });
        server->on("/save", HTTP_POST, [this] { handleSave(); });
        server->on("/preview", HTTP_POST, [this] { handlePreview(); });
        // GET only. The matching POST /update - the one that actually writes the
        // image - is registered by RemoteDevelopmentService and is not touched here.
        server->on("/update", HTTP_GET, [this] { handleUpdatePage(); });
    }

    void open(const uint32_t nowMs) {
        active = true;
        lastActivity = nowMs;
        restartArmed = false;
        resetPreview();
    }

    void close() {
        active = false;
        resetPreview();
    }

    // ---- LED preview state, read by PlayerSetupView ------------------------

    // Returns whether the preview state changed since the last call, and clears
    // the flag - the view's one dirty-check per render, so a slot update that
    // lands between renders is never missed nor rendered twice.
    bool takePreviewDirty() {
        const bool wasDirty = previewDirty;
        previewDirty = false;
        return wasDirty;
    }

    bool hasPreview() const {
        return previewActive;
    }

    const PreviewSlot &previewSlot(const uint8_t side) const {
        return previewSlots[side < 2 ? side : 0];
    }

    uint32_t lastActivityMs() const {
        return lastActivity;
    }

    /**
     * Called by whatever else counts as someone using the editor - the firmware
     * upload does, and it is the slow case: a fumbling recipient who opens PROFILE
     * and then works only on the update screen must not have the AP closed under
     * them by the 15-minute idle timer.
     */
    void noteActivity() {
        lastActivity = millis();
    }

    /**
     * Pumped from main.cpp right after the networking loop. The restart is
     * deferred by a few hundred ms so the socket flushes and the phone sees the
     * confirmation instead of a dropped connection.
     */
    void loop() {
        if (restartArmed && static_cast<int32_t>(millis() - restartAtMs) >= 0) {
            restartArmed = false;
            safeRestart();
        }
    }

private:
    enum : uint32_t { RESTART_DELAY_MS = 300 };
    enum : uint8_t { NAV_PROFILE = 0, NAV_UPDATE = 1 };

    // Every plain-text reply carries Polish, so the charset is not optional - a
    // bare "text/plain" is read as latin-1 by most browsers.
    static constexpr const char *TEXT_PLAIN_PL = "text/plain; charset=utf-8";

    PlayerRoster &roster;
    PreferencesManager &preferencesManager;
    WebServer *server = nullptr;

    bool active = false;
    uint32_t lastActivity = 0;
    bool restartArmed = false;
    uint32_t restartAtMs = 0;

    // side 0 = left, 1 = right (matches the P<l>P<r> glyph layout). No per-id
    // memory beyond these two: every POST carries the colour to show.
    PreviewSlot previewSlots[2];
    uint8_t previewLatest = 0;
    bool previewActive = false;
    bool previewDirty = false;

    void armRestart() {
        restartAtMs = millis() + RESTART_DELAY_MS;
        restartArmed = true;
    }

    // Called on open() and close() so a leftover pair from the previous PROFILE
    // visit never survives into the next one. previewDirty=true so the view's
    // next render falls back to the static word.
    void resetPreview() {
        previewSlots[0] = PreviewSlot{};
        previewSlots[1] = PreviewSlot{};
        previewLatest = 0;
        previewActive = false;
        previewDirty = true;
    }

    void reject(const char *reason) const {
        server->send(400, TEXT_PLAIN_PL, reason);
    }

    // Escaped for an HTML attribute. The SSID comes back out of NVS, so it is data.
    void sendHtmlAttr(const char *text) const {
        String out;
        for (const char *c = text; *c != '\0'; c++) {
            if (*c == '&') out += F("&amp;");
            else if (*c == '<') out += F("&lt;");
            else if (*c == '>') out += F("&gt;");
            else if (*c == '"') out += F("&quot;");
            else out += *c;
        }
        if (out.length() > 0) {
            server->sendContent(out);
        }
    }

    // Escaped for a JavaScript string literal. `<` is escaped too, so a name can
    // never close the script element.
    static void appendJsString(String &out, const char *text) {
        out += '"';
        for (const char *c = text; *c != '\0'; c++) {
            if (*c == '\\' || *c == '"') {
                out += '\\';
                out += *c;
            } else if (*c == '<') {
                out += "\\u003C";
            } else {
                out += *c;
            }
        }
        out += '"';
    }

    // ---- shared shell ------------------------------------------------------

    /**
     * Head, stylesheet and nav bar, shared by both screens. Sent with an unknown
     * content length so the rest can be streamed in chunks rather than assembled
     * into one large String.
     */
    void beginPage(const int status, const char *title, const uint8_t navIndex) const {
        server->sendHeader("Cache-Control", "no-store");
        server->setContentLength(CONTENT_LENGTH_UNKNOWN);
        server->send(status, "text/html; charset=utf-8", "");

        server->sendContent(F(
            "<!DOCTYPE html><html lang=\"pl\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>"));
        server->sendContent(title);
        server->sendContent(F(
            "</title><style>"
            // Light on purpose, and pinned: the board lives in a bright hall and the
            // page is read at arm's length, so it must not follow a phone's dark mode.
            ":root{color-scheme:light;--bg:#f6f2ea;--card:#fffdf9;--line:#e6dfd2;"
            "--text:#2f2b25;--muted:#8a8175;--accent:#2f7d4f;--danger:#b4472f}"
            "*{box-sizing:border-box}"
            "body{margin:0;padding:16px 16px 40px;background:var(--bg);color:var(--text);"
            "font:16px/1.45 system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;"
            "-webkit-text-size-adjust:100%}"
            "nav{max-width:520px;margin:0 auto 18px;display:flex;gap:8px}"
            "nav a{flex:1;display:flex;align-items:center;justify-content:center;text-align:center;"
            "padding:10px 8px;font-size:13px;line-height:1.25;text-decoration:none;color:var(--muted);"
            "background:var(--card);border:1px solid var(--line);border-radius:12px}"
            "nav a.on{color:#fff;background:var(--accent);border-color:var(--accent);font-weight:600}"
            "header{max-width:520px;margin:0 auto 18px}"
            "h1{font-size:24px;margin:0 0 4px;letter-spacing:-.01em}"
            "p.sub{margin:0;color:var(--muted);font-size:14px}"
            "main{max-width:520px;margin:0 auto}"
            ".note{background:var(--card);border:1px solid var(--line);border-radius:14px;"
            "padding:14px;color:var(--muted);font-size:14px}"));

        server->sendContent(F(
            ".row{display:flex;align-items:center;gap:10px;background:var(--card);"
            "border:1px solid var(--line);border-radius:14px;padding:10px;margin-bottom:10px;"
            "box-shadow:0 1px 2px rgba(60,50,30,.05)}"
            ".sw{width:40px;height:40px;border-radius:12px;flex:none;padding:0;cursor:pointer;"
            "border:2px solid rgba(0,0,0,.14);box-shadow:inset 0 0 0 2px #fff}"
            ".row input{flex:1;min-width:0;padding:10px 12px;font-size:17px;color:var(--text);"
            "background:#fff;border:1px solid var(--line);border-radius:10px}"
            ".row input:focus{outline:2px solid var(--accent);outline-offset:1px;border-color:transparent}"
            ".ic{width:38px;height:38px;flex:none;padding:0;font-size:16px;line-height:1;"
            "background:#f3efe6;color:var(--muted);border:1px solid var(--line);border-radius:10px;"
            "cursor:pointer}"
            ".ic:active{background:#e9e3d6}"
            ".ic.del{color:var(--danger)}"
            "button{font:inherit}"
            "#add{width:100%;padding:12px;margin-top:2px;background:transparent;color:var(--muted);"
            "border:1.5px dashed var(--line);border-radius:14px;cursor:pointer}"
            "#cnt{display:block;text-align:center;color:var(--muted);font-size:13px;margin:10px 0 4px}"
            "#msg{min-height:22px;text-align:center;font-size:14px;color:var(--danger);margin:6px 0}"
            ".primary{width:100%;padding:16px;margin-top:6px;font-size:17px;font-weight:600;color:#fff;"
            "background:var(--accent);border:0;border-radius:14px;cursor:pointer;"
            "box-shadow:0 2px 6px rgba(47,125,79,.28)}"
            "#reset{width:100%;padding:12px;margin-top:10px;color:var(--muted);background:transparent;"
            "border:1px solid var(--line);border-radius:14px;cursor:pointer}"));

        server->sendContent(F(
            ".card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px}"
            ".card input[type=file]{width:100%;padding:12px;font-size:15px;background:#fff;"
            "border:1px dashed var(--line);border-radius:12px;margin-bottom:12px}"
            "h2.sec{font-size:17px;margin:26px 0 6px}"
            ".card label{display:block;font-size:13px;color:var(--muted);margin-bottom:5px}"
            ".card input[type=text],.card input[type=password]{width:100%;padding:11px 12px;"
            "font-size:16px;color:var(--text);background:#fff;border:1px solid var(--line);"
            "border-radius:10px;margin-bottom:12px}"
            "p.fwv{margin:0 0 12px;color:var(--muted);font-size:13px}"
            "#otabar{display:none;width:100%;height:14px;margin-top:12px}"
            ".danger{display:none;width:100%;padding:13px;margin-top:10px;font-size:16px;color:#fff;"
            "background:var(--danger);border:0;border-radius:14px;cursor:pointer}"
            "#otamsg{min-height:22px;font-size:14px;color:var(--muted);margin-top:10px;"
            "word-break:break-word}"
            // The picker is a tap-to-open sheet rather than a dropdown: a colour is
            // far easier to recognise than its name, and 12 swatches do not fit in a row.
            "#pal{position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(47,43,37,.45);"
            "display:none;align-items:flex-end;justify-content:center;z-index:10}"
            "#palbox{background:var(--card);border-radius:20px 20px 0 0;padding:18px 16px 24px;"
            "width:100%;max-width:520px;box-shadow:0 -4px 24px rgba(60,50,30,.18)}"
            "#palbox h2{font-size:17px;margin:0 0 14px;text-align:center}"
            "#palgrid{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin-bottom:16px}"
            ".pc{background:none;border:0;padding:0;cursor:pointer}"
            ".pc i{display:block;width:100%;height:56px;border-radius:14px;"
            "border:3px solid rgba(0,0,0,.10)}"
            ".pc.on i{border-color:var(--accent)}"
            ".pc span{display:block;font-size:11px;color:var(--muted);margin-top:5px;overflow:hidden;"
            "text-overflow:ellipsis;white-space:nowrap}"
            "#palcustom{width:100%;padding:13px;margin-bottom:10px;color:var(--muted);"
            "background:transparent;border:1px solid var(--line);border-radius:14px;cursor:pointer}"
            // .on marks a custom (non-preset) selection: the button itself becomes
            // the swatch, same idea as .pc.on's accent ring on a grid entry.
            "#palcustom.on{color:#fff;border-color:transparent}"
            ".palbtns{display:flex;gap:10px}"
            ".palbtns button{flex:1;padding:13px;border-radius:14px;cursor:pointer;"
            "border:1px solid var(--line)}"
            "#palclose{color:var(--muted);background:transparent}"
            "#palok{color:#fff;background:var(--accent);border-color:var(--accent);font-weight:600}"
            "</style></head><body><nav>"));

        server->sendContent(navIndex == NAV_PROFILE
                                ? F("<a class=\"on\" href=\"/\">Profile</a>"
                                    "<a href=\"/update\">Aktualizacja oprogramowania</a>")
                                : F("<a href=\"/\">Profile</a>"
                                    "<a class=\"on\" href=\"/update\">Aktualizacja oprogramowania</a>"));
        server->sendContent(F("</nav>"));
    }

    void endPage() const {
        server->sendContent(F("</body></html>"));
        server->sendContent("");
    }

    // ---- profile editor ----------------------------------------------------

    void handleRoot() {
        if (!active) {
            // Still a 503 - the editor really is unavailable - but with the shell, so
            // the firmware screen stays one tap away instead of a dead end.
            beginPage(503, "Profile", NAV_PROFILE);
            server->sendContent(F(
                "<main><div class=\"note\">Ekran profili nie jest teraz otwarty na tablicy."
                "<br><br>Wybierz <b>PROFILE</b> w menu tablicy i otwórz tę stronę "
                "ponownie.</div></main>"));
            endPage();
            return;
        }

        lastActivity = millis();

        beginPage(200, "Profile", NAV_PROFILE);
        server->sendContent(F(
            "<header><h1>Profile</h1>"
            "<p class=\"sub\">Do 32 pozycji. Zapis zrestartuje tablicę.</p></header>"
            "<main><div id=\"rows\"></div>"
            "<button type=\"button\" id=\"add\">+ Dodaj profil</button>"
            "<span id=\"cnt\"></span>"
            "<div id=\"msg\"></div>"
            "<button type=\"button\" id=\"save\" class=\"primary\">Zapisz i zrestartuj</button>"
            "<button type=\"button\" id=\"reset\">Przywróć profile fabryczne</button></main>"
            "<div id=\"pal\"><div id=\"palbox\"><h2>Wybierz kolor</h2><div id=\"palgrid\"></div>"
            "<button type=\"button\" id=\"palcustom\">Własny kolor</button>"
            "<input type=\"color\" id=\"palpick\" style=\"display:none\">"
            "<div class=\"palbtns\"><button type=\"button\" id=\"palclose\">Anuluj</button>"
            "<button type=\"button\" id=\"palok\">Wybierz</button></div>"
            "</div></div>"
            "<script>"));

        server->sendContent(buildData());

        server->sendContent(F(
            "function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;')"
            ".replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}"
            // R[i][1] is a palette index (number) for a preset colour, or a
            // '#RRGGBB' string for a custom one - see buildData()/isPreset().
            "function hx(c){return typeof c=='number'?PAL[c][1]:c;}"
            "function render(){var h='';for(var i=0;i<R.length;i++){"
            "h+='<div class=\"row\">'"
            "+'<button type=\"button\" class=\"sw\" id=\"sw'+i+'\" style=\"background:'+hx(R[i][1])+'\""
            " onclick=\"pick('+i+')\"></button>'"
            "+'<input maxlength=\"9\" value=\"'+esc(R[i][0])+'\" oninput=\"setName('+i+',this.value)\">'"
            "+'<button type=\"button\" class=\"ic\" onclick=\"mv('+i+',-1)\">&#9650;</button>'"
            "+'<button type=\"button\" class=\"ic\" onclick=\"mv('+i+',1)\">&#9660;</button>'"
            "+'<button type=\"button\" class=\"ic del\" onclick=\"del('+i+')\">&times;</button></div>';}"
            "document.getElementById('rows').innerHTML=h;"
            "document.getElementById('cnt').textContent=R.length+' / '+MAX;}"
            // No re-render on keystroke: that would drop focus mid-word.
            "function setName(i,v){R[i][0]=v;}"
            // cur = row being edited; orig = its colour when the dialog opened
            // (Anuluj target); sel = the pending selection (Wybierz target).
            "var cur=-1,orig=0,sel=0;"
            "function pick(i){cur=i;orig=sel=R[i][1];renderPal();"
            "document.getElementById('pal').style.display='flex';}"
            "function renderPal(){var h='';"
            "for(var k=0;k<PAL.length;k++){"
            "h+='<button type=\"button\" class=\"pc'+(typeof sel=='number'&&sel==k?' on':'')+'\""
            " onclick=\"swatchClick('+k+')\">'"
            "+'<i style=\"background:'+PAL[k][1]+'\"></i><span>'+esc(PAL[k][0])+'</span></button>';}"
            "document.getElementById('palgrid').innerHTML=h;"
            "var cb=document.getElementById('palcustom');"
            "if(typeof sel=='string'){cb.style.background=sel;cb.classList.add('on');}"
            "else{cb.style.background='';cb.classList.remove('on');}}"
            "function swatchClick(k){sel=k;renderPal();preview(cur,PAL[k][1]);}"
            "function pickChanged(){sel=document.getElementById('palpick').value.toUpperCase();"
            "renderPal();preview(cur,sel);}"
            "document.getElementById('palcustom').onclick=function(){"
            "var p=document.getElementById('palpick');p.value=hx(sel).toLowerCase();p.click();};"
            // 'change' too: iOS Safari's native picker fires only on close, not per drag.
            "document.getElementById('palpick').oninput=pickChanged;"
            "document.getElementById('palpick').onchange=pickChanged;"
            "function closePal(){document.getElementById('pal').style.display='none';cur=-1;}"
            "document.getElementById('palok').onclick=function(){"
            "R[cur][1]=sel;document.getElementById('sw'+cur).style.background=hx(sel);closePal();};"
            "document.getElementById('palclose').onclick=function(){preview(cur,hx(orig));closePal();};"
            "document.getElementById('pal').onclick=function(e){"
            "if(e.target.id=='pal'){preview(cur,hx(orig));closePal();}};"
            // One /preview in flight; a colour arriving mid-request replaces
            // whatever was pending rather than queuing, so drag events never pile up.
            "var pvBusy=false,pvNext=null;"
            "function preview(i,h){pvNext=[i,h];if(!pvBusy)pvSend();}"
            "function pvSend(){if(!pvNext)return;var n=pvNext;pvNext=null;pvBusy=true;"
            "var x=new XMLHttpRequest();x.open('POST','/preview',true);x.timeout=3000;"
            "x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');"
            "x.onloadend=function(){pvBusy=false;pvSend();};"
            // Best effort: a dropped preview leaves the LEDs one frame behind,
            // never the page - so failures are silent by design.
            "x.send('playerId='+n[0]+'&color='+encodeURIComponent(n[1]));}"
            "function mv(i,d){var j=i+d;if(j<0||j>=R.length)return;var t=R[i];R[i]=R[j];R[j]=t;render();}"
            "function del(i){if(R.length<2){msg('Musi zosta\\u0107 co najmniej jeden profil.');return;}"
            "R.splice(i,1);render();}"
            "function msg(t){document.getElementById('msg').textContent=t;}"
            "function post(b){var x=new XMLHttpRequest();x.open('POST','/save',true);"
            "x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');"
            "x.onload=function(){msg(x.status==200?x.responseText:'B\\u0142\\u0105d '+x.status+': '+x.responseText);};"
            "x.onerror=function(){msg('Po\\u0142\\u0105czenie przerwane - tablica prawdopodobnie si\\u0119 restartuje.');};"
            "x.send(b);}"
            "document.getElementById('add').onclick=function(){"
            "if(R.length>=MAX){msg('Osi\\u0105gni\\u0119to limit 32.');return;}"
            "R.push(['Gracz'+(R.length+1),R.length%PAL.length,0]);render();};"
            "document.getElementById('save').onclick=function(){"
            "for(var i=0;i<R.length;i++){if(!R[i][0].trim()){msg('Wiersz '+(i+1)+': brak imienia.');return;}}"
            // Label-colon form: Polish numerals decline, so "N profili" is wrong for some N.
            "if(!confirm('Zapisa\\u0107 list\\u0119. Liczba pozycji: '+R.length"
            "+'. Tablica zostanie zrestartowana.'))return;"
            "var b='count='+R.length;"
            "for(var i=0;i<R.length;i++){b+='&n'+i+'='+encodeURIComponent(R[i][0].trim())"
            "+'&c'+i+'='+encodeURIComponent(R[i][1])+'&u'+i+'='+R[i][2];}"
            "msg('Zapisywanie...');post(b);};"
            "document.getElementById('reset').onclick=function(){"
            "if(!confirm('Przywr\\u00f3ci\\u0107 profile fabryczne? Tablica zostanie zrestartowana.'))return;"
            "msg('Przywracanie...');post('reset=1');};"
            "render();"
            "</script>"));

        endPage();
    }

    // ---- firmware update ---------------------------------------------------

    /**
     * The firmware upload screen. It posts to POST /update, which
     * RemoteDevelopmentService owns - the same endpoint
     * `pio run -t upload -e lolin_s2_mini_ota` curls a multipart body to.
     *
     * Written for the person this whole path exists for: someone who was emailed a
     * .bin and has never seen a build directory. That is why the running version is
     * on the page (the only confirmation an update landed that they will ever see),
     * why a refusal says what to do rather than what the header held, and why the
     * bypass appears only *after* a refusal - offered up front it would simply be
     * pressed, and the check would buy nothing.
     *
     * Ungated on purpose: unlike the profile editor this has to stay reachable
     * whenever networking is up, including when no screen is open on the board.
     */
    void handleUpdatePage() {
        // Browsing this screen is using the editor; without this the mode closes
        // under someone who came here and stayed.
        lastActivity = millis();

        beginPage(200, "Aktualizacja oprogramowania", NAV_UPDATE);
        server->sendContent(F(
            "<header><h1>Aktualizacja oprogramowania</h1>"
            "<p class=\"sub\">Wybierz plik firmware.bin i wyślij go na tablicę.</p></header>"
            "<main><div class=\"card\">"
            "<p class=\"fwv\">Wersja na tablicy: <b>"));
        server->sendContent(FW_VERSION);
        server->sendContent(F(
            "</b></p>"
            "<input type=\"file\" id=\"fw\" accept=\".bin\">"
            "<button type=\"button\" id=\"otasend\" class=\"primary\">Wgraj i zrestartuj</button>"
            "<progress id=\"otabar\" max=\"100\" value=\"0\"></progress>"
            "<div id=\"otamsg\"></div>"
            "<button type=\"button\" id=\"otaforce\" class=\"danger\">Wgraj mimo to</button>"
            "</div>"));

        sendWifiCard();

        server->sendContent(F(
            "</main>"
            "<script>"
            "var busy=false,bad=null,t0=0;"
            "function el(i){return document.getElementById(i);}"
            "function om(t){el('otamsg').textContent=t;}"
            "function bar(p){var b=el('otabar');b.style.display=p<0?'none':'block';if(p>=0)b.value=p;}"
            "function hideForce(){bad=null;el('otaforce').style.display='none';}"
            "el('fw').onchange=function(){hideForce();om('');bar(-1);};"
            "function send(f,force){"
            "busy=true;hideForce();el('otasend').disabled=true;bar(0);"
            "om('Wysy\\u0142anie pliku...');"
            "var d=new FormData();d.append('update',f,f.name);"
            "var x=new XMLHttpRequest();"
            "x.open('POST','/update'+(force?'?force=1':''),true);"));

        server->sendContent(F(
            // Progress only. 100 % here means the browser has finished writing to its
            // own socket, not that the board has finished reading it, so nothing is
            // ever decided on this event - the verdict arrives in onload.
            "x.upload.onprogress=function(e){if(e.lengthComputable)bar(Math.round(e.loaded*100/e.total));};"
            "x.onload=function(){busy=false;el('otasend').disabled=false;"
            "if(x.status==200){bar(100);"
            "om('Wgrano. Tablica restartuje si\\u0119, poczekaj...');t0=Date.now();wait();return;}"
            "bar(-1);om(x.responseText||('B\\u0142\\u0105d '+x.status));"
            // Only a refused image can be forced. A connection that broke tells us
            // nothing about the file, so it gets no bypass.
            "if(x.status==400){bad=f;el('otaforce').style.display='block';}};"
            "x.onerror=x.onabort=function(){busy=false;el('otasend').disabled=false;bar(-1);hideForce();"
            "om('Po\\u0142\\u0105czenie przerwane. Sprawd\\u017a, czy telefon jest nadal "
            "po\\u0142\\u0105czony z sieci\\u0105 tablicy, i spr\\u00f3buj ponownie.');};"
            "x.send(d);}"));

        server->sendContent(F(
            // Any answer at all proves the board is back: GET / legitimately replies
            // 503 when the profile screen is not the open one.
            "function wait(){var x=new XMLHttpRequest();"
            "x.open('GET','/?ping='+Date.now(),true);x.timeout=4000;"
            "x.onload=function(){location.reload();};"
            "x.onerror=x.ontimeout=function(){"
            "if(Date.now()-t0>120000){om('Tablica nie odpowiada. Po\\u0142\\u0105cz telefon ponownie "
            "z sieci\\u0105 tablicy i od\\u015bwie\\u017c stron\\u0119.');return;}"
            "setTimeout(wait,2000);};x.send();}"
            "el('otasend').onclick=function(){if(busy)return;"
            "var f=el('fw').files[0];"
            "if(!f){om('Najpierw wybierz plik .bin.');return;}"
            "if(!/\\.bin$/i.test(f.name)){om('To nie jest plik .bin.');return;}"
            "send(f,false);};"
            "el('otaforce').onclick=function(){if(busy||!bad)return;"
            "if(!confirm('Ten plik nie przeszed\\u0142 sprawdzenia. Wgranie go mo\\u017ce sprawi\\u0107, "
            "\\u017ce tablica przestanie dzia\\u0142a\\u0107. Wgra\\u0107 mimo to?'))return;"
            "send(bad,true);};"
            "</script>"));
        endPage();
    }

    /**
     * The WiFi credentials form, on this screen and nowhere else. The house network
     * is an OTA convenience - it lets the board be flashed without walking over to
     * it - and nothing else uses it: the profile editor always raises its own AP,
     * so it is reached by the placard QR codes rather than by a guessed IP address.
     * OTA itself works over either.
     *
     * Posts to /connect, which RemoteDevelopmentService owns: it saves the
     * credentials and reboots. A plain form, so it works with JavaScript off too.
     */
    void sendWifiCard() {
        server->sendContent(F(
            "<h2 class=\"sec\">Sie\u0107 WiFi</h2>"
            "<p class=\"sub\">Tylko do aktualizacji przez sie\u0107 domow\u0105. "
            "Ekran profili zawsze dzia\u0142a na w\u0142asnym AP.</p>"
            "<form class=\"card\" method=\"POST\" action=\"/connect\">"
            "<label>Nazwa sieci</label>"
            "<input type=\"text\" name=\"ssid\" maxlength=\"63\" value=\""));

        sendHtmlAttr(preferencesManager.settings.wifiSSID);

        server->sendContent(F(
            "\">"
            "<label>Has\u0142o</label>"
            "<input type=\"password\" name=\"password\" maxlength=\"63\">"
            "<button type=\"submit\" class=\"primary\">Zapisz i po\u0142\u0105cz</button>"
            "</form>"));
    }

    // ---- LED preview ---------------------------------------------------------

    /**
     * State only - never NVS, never a draw. PlayerSetupView polls
     * takePreviewDirty() on its own cadence and renders from previewSlots. Same
     * `active` gate and 503 as /save: the preview must not work with the AP
     * closed, since that means nobody has the roster editor open to receive it.
     */
    void handlePreview() {
        if (!active) {
            server->send(503, TEXT_PLAIN_PL, "Ekran profili nie jest otwarty na tablicy.");
            return;
        }

        lastActivity = millis();

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

        applyPreview(static_cast<uint8_t>(playerId), color);
        previewDirty = true;

        server->send(204, TEXT_PLAIN_PL, "");
    }

    /**
     * Slot logic locked in task #51. An id already shown updates in place and
     * becomes latest; a first preview since open() pairs the clicked id with the
     * lowest other roster id (in its stored colour); otherwise the non-latest
     * slot is replaced.
     */
    void applyPreview(const uint8_t playerId, const Color color) {
        for (uint8_t side = 0; side < 2; side++) {
            if (previewSlots[side].used && previewSlots[side].id == playerId) {
                previewSlots[side].color = color;
                previewLatest = side;
                previewActive = true;
                return;
            }
        }

        if (!previewActive) {
            previewSlots[0] = PreviewSlot{playerId, color, true};

            // Lowest other roster id, in its own stored colour - even if that
            // happens to match `color`, per the locked spec.
            PreviewSlot right;
            const std::vector<UserProfile *> &players = roster.profiles();
            for (size_t i = 0; i < players.size(); i++) {
                if (players[i]->getId() != playerId) {
                    right = PreviewSlot{players[i]->getId(), players[i]->getColor(), true};
                    break;
                }
            }
            previewSlots[1] = right;

            // Right counts as latest even when no other player exists, so the
            // next distinct id replaces the left slot rather than the empty right.
            previewLatest = 1;
            previewActive = true;
            return;
        }

        const uint8_t target = static_cast<uint8_t>(1 - previewLatest);
        previewSlots[target] = PreviewSlot{playerId, color, true};
        previewLatest = target;
    }

    // ---- data --------------------------------------------------------------

    String buildData() const {
        String out;
        out.reserve(1024);

        out += F("var MAX=");
        out += PlayerRosterLimits::MAX_PLAYERS;
        out += F(";var PAL=[");

        uint8_t paletteCount = 0;
        const PlayerPalette::PaletteEntry *palette = PlayerPalette::table(paletteCount);
        for (uint8_t i = 0; i < paletteCount; i++) {
            char hex[8];
            PlayerPalette::toHex(palette[i].color, hex);

            if (i > 0) out += ',';
            out += '[';
            appendJsString(out, palette[i].name);
            out += ',';
            appendJsString(out, hex);
            out += ']';
        }

        out += F("];var R=[");

        const std::vector<UserProfile *> &players = roster.profiles();
        for (size_t i = 0; i < players.size(); i++) {
            uint8_t index = 0;
            // Exact, not nearest: a stored colour that is still a preset renders
            // and re-saves as that preset's index; anything else - including a
            // roster stored before the palette changed - is now a custom swatch,
            // shown and re-saved as its own hex rather than snapped onto the
            // closest entry.
            const bool preset = PlayerPalette::isPreset(players[i]->getColor(), index);

            if (i > 0) out += ',';
            out += '[';
            appendJsString(out, players[i]->getName());
            out += ',';
            if (preset) {
                out += index;
            } else {
                char hex[8];
                PlayerPalette::toHex(players[i]->getColor(), hex);
                appendJsString(out, hex);
            }
            // Third field is the identity, carried out to the page and back so a
            // reorder or a rename moves the row without changing who it is.
            out += ',';
            out += players[i]->getUid();
            out += ']';
        }

        out += F("];");
        return out;
    }

    /**
     * Server-authoritative and atomic. Every field is checked on its own - never
     * trusted because the posted `count` said so - and the whole sizeof(PlayersData)
     * blob is staged in RAM before a single putBytes. Any rejection leaves NVS untouched.
     */
    void handleSave() {
        if (!active) {
            server->send(503, TEXT_PLAIN_PL, "Ekran profili nie jest otwarty na tablicy.");
            return;
        }

        lastActivity = millis();

        if (server->hasArg("reset") && server->arg("reset") == "1") {
            if (!roster.resetToDefaults()) {
                server->send(500, TEXT_PLAIN_PL, "Nie udało się zapisać w NVS.");
                return;
            }

            server->send(200, TEXT_PLAIN_PL, "Przywrócono profile fabryczne. Restart...");
            armRestart();
            return;
        }

        if (!server->hasArg("count")) {
            reject("Brak pola count.");
            return;
        }

        const long count = server->arg("count").toInt();
        if (count < 1 || count > PlayerRosterLimits::MAX_PLAYERS) {
            reject("Pole count musi być w zakresie 1..32.");
            return;
        }

        char key[8];

        // An extra name beyond `count` means the page and the body disagree; that is
        // a malformed request, not something to silently truncate.
        snprintf(key, sizeof(key), "n%ld", count);
        if (server->hasArg(key)) {
            reject("Więcej imion niż wynosi count.");
            return;
        }

        PlayersData staged;
        memset(&staged, 0, sizeof(staged));
        staged.version = PlayerRosterLimits::BLOB_VERSION;
        staged.count = static_cast<uint8_t>(count);

        const uint8_t paletteCount = PlayerPalette::count();

        for (uint8_t i = 0; i < staged.count; i++) {
            snprintf(key, sizeof(key), "n%u", static_cast<unsigned>(i));
            if (!server->hasArg(key)) {
                reject("Brakuje imienia w jednym z wierszy.");
                return;
            }

            String name = server->arg(key);
            name.trim();

            if (name.length() < 1 || name.length() > PlayerRosterLimits::NAME_SIZE - 1) {
                reject("Imię musi mieć od 1 do 9 znaków.");
                return;
            }

            for (size_t c = 0; c < name.length(); c++) {
                if (name[c] < 0x20 || name[c] > 0x7E) {
                    // These names reach the LED, OLED and e-paper fonts, and none of
                    // them has an accented glyph.
                    reject("Imię bez polskich znaków - tablica ich nie wyświetli.");
                    return;
                }
            }

            snprintf(key, sizeof(key), "c%u", static_cast<unsigned>(i));
            if (!server->hasArg(key)) {
                reject("Brakuje koloru w jednym z wierszy.");
                return;
            }

            const String colorArg = server->arg(key);
            Color color;

            // Two wire formats: a palette index (unchanged), or #RRGGBB for a
            // colour previewed via the native picker - see PlayerPalette::fromHex.
            if (colorArg.length() > 0 && colorArg[0] == '#') {
                if (!PlayerPalette::fromHex(colorArg, color)) {
                    reject("Błędny kolor.");
                    return;
                }
            } else {
                if (colorArg.length() < 1 || colorArg.length() > 2) {
                    reject("Błędny kolor.");
                    return;
                }

                for (size_t c = 0; c < colorArg.length(); c++) {
                    if (colorArg[c] < '0' || colorArg[c] > '9') {
                        reject("Błędny kolor.");
                        return;
                    }
                }

                const long colorIndex = colorArg.toInt();
                if (colorIndex < 0 || colorIndex >= paletteCount) {
                    reject("Błędny kolor.");
                    return;
                }

                color = PlayerPalette::at(static_cast<uint8_t>(colorIndex)).color;
            }

            strncpy(staged.entries[i].name, name.c_str(), PlayerRosterLimits::NAME_SIZE - 1);
            staged.entries[i].name[PlayerRosterLimits::NAME_SIZE - 1] = '\0';

            staged.entries[i].r = color.r;
            staged.entries[i].g = color.g;
            staged.entries[i].b = color.b;

            snprintf(key, sizeof(key), "u%u", static_cast<unsigned>(i));
            if (!server->hasArg(key)) {
                reject("Brakuje identyfikatora w jednym z wierszy.");
                return;
            }

            const String uidArg = server->arg(key);
            if (uidArg.length() < 1 || uidArg.length() > 10) {
                reject("B\u0142\u0119dny identyfikator.");
                return;
            }

            uint64_t uid = 0;
            for (size_t c = 0; c < uidArg.length(); c++) {
                if (uidArg[c] < '0' || uidArg[c] > '9') {
                    reject("B\u0142\u0119dny identyfikator.");
                    return;
                }
                uid = uid * 10 + static_cast<uint32_t>(uidArg[c] - '0');
            }

            if (uid > 0xFFFFFFFFULL) {
                reject("B\u0142\u0119dny identyfikator.");
                return;
            }

            // Two rows claiming one identity is a malformed body, not something to
            // silently merge - it would make one player vanish into another.
            if (uid != 0 && PlayerRoster::isUidTaken(staged, i, static_cast<uint32_t>(uid))) {
                reject("Powt\u00f3rzony identyfikator gracza.");
                return;
            }

            // 0 means "new row". The device mints identities, never the page.
            staged.entries[i].uid = uid != 0
                                        ? static_cast<uint32_t>(uid)
                                        : PlayerRoster::generateUid(staged, i);
        }

        if (!roster.save(staged)) {
            server->send(500, TEXT_PLAIN_PL, "Nie udało się zapisać w NVS.");
            return;
        }

        server->send(200, TEXT_PLAIN_PL, "Zapisano. Restart...");
        armRestart();
    }
};

#endif //PLAYER_SETUP_WEB_UI_H
