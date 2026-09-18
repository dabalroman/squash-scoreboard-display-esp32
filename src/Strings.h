#ifndef STRINGS_H
#define STRINGS_H

/**
 * Every user-visible string on the device, in one place.
 *
 * One language per build: -DLANG_PL (set in platformio.ini for both envs) selects
 * the Polish table; the English branch is kept for reference and never compiled.
 * Never turn this into a two-row table indexed at runtime - that would ship both
 * languages in the image.
 *
 * No Polish diacritics anywhere: every GFX font in use declares range 0x20-0x7E
 * and GlyphMasks.h has no accented glyphs. Words that would need an ogonek or a
 * stroke were replaced, not stripped (SIATKA, not siatkowka; NISKA, not slaba).
 *
 * Names are screen-scoped. The _OLED variants exist because the OLED list is
 * space-padded for centring at a fixed x, while the e-paper rows are not.
 *
 * The LED_* words belong here for the same reason the text does: the front LEDs,
 * the OLED and the e-paper are readable at the same time and must say the same
 * thing, so switching language has to move all three together. They are plain
 * 4-character strings run through LedText::toWord; the casing is literal,
 * because the glyph table has only one form of most letters, and it has no
 * W, K, M or V at all - which is why Return is COFNIJ and not WSTECZ.
 *
 * Out of scope on purpose: printLn/telnet/serial output, the WiFi config web
 * page, player names, the AP SSID/password, and the BAT / FW abbreviations.
 */
namespace Str {
#if LANG_PL

    // CONFIG menu
    constexpr const char *const CONFIG_MENU_TITLE = "OPCJE";
    constexpr const char *const CONFIG_ROW_BRIGHTNESS_LABEL = "LED";
    constexpr const char *const CONFIG_ROW_BUZZER_LABEL = "BUZZER";
    constexpr const char *const CONFIG_ROW_WIFI_LABEL = "WIFI";
    constexpr const char *const CONFIG_ROW_REBOOT_LABEL = "RESTART";
    constexpr const char *const CONFIG_ROW_RETURN_LABEL = "COFNIJ";
    constexpr const char *const CONFIG_VALUE_ON = "TAK";
    constexpr const char *const CONFIG_VALUE_OFF = "NIE";
    constexpr const char *const CONFIG_OPTION_BRIGHTNESS_OLED = "LED";
    constexpr const char *const CONFIG_OPTION_BUZZER_OLED = "BUZZER";
    constexpr const char *const CONFIG_OPTION_WIFI_OLED = "WIFI";
    constexpr const char *const CONFIG_OPTION_REBOOT_OLED = " [RESTART]";
    constexpr const char *const CONFIG_OPTION_RETURN_OLED = " [COFNIJ]";

    // Mode selector
    constexpr const char *const MODE_MENU_TITLE = "TRYB";
    constexpr const char *const MODE_OPTION_SQUASH = "SQUASH";
    constexpr const char *const MODE_OPTION_VOLLEYBALL = "SIATKA";
    constexpr const char *const MODE_OPTION_SHORT_VOLLEYBALL = "SIATKA 15";
    constexpr const char *const MODE_OPTION_PADEL = "PADEL";
    constexpr const char *const MODE_OPTION_CONFIG = "OPCJE";
    constexpr const char *const MODE_OPTION_SQUASH_OLED = "  SQUASH  ";
    constexpr const char *const MODE_OPTION_VOLLEYBALL_OLED = "  SIATKA  ";
    constexpr const char *const MODE_OPTION_SHORT_VOLLEYBALL_OLED = " SIATKA 15";
    constexpr const char *const MODE_OPTION_PADEL_OLED = "  PADEL  ";
    constexpr const char *const MODE_OPTION_PLAYERS = "PROFILE";
    constexpr const char *const MODE_OPTION_PLAYERS_OLED = " [PROFILE]";
    constexpr const char *const MODE_OPTION_CONFIG_OLED =  " [OPCJE]   ";

    // Player picker
    constexpr const char *const PLAYERS_ROW_START = "START";
    constexpr const char *const PLAYERS_OPTION_START_OLED = " [START] ";
    // Label-colon form on purpose: Polish numerals decline (1 gracz / 2-4 gracze /
    // 5+ graczy), so anything of the shape "%u <noun>" is wrong for some counts.
    constexpr const char *const PLAYERS_FOOTER_COUNT_FMT = "W GRZE: %u";

    // E-paper match score labels. Squash and volleyball share one English word but
    // not one Polish word, hence the per-sport names.
    // One label per thing counted, shared by every sport that counts it: squash
    // and volleyball only ever count sets, padel counts gems within a set and
    // sets on the screen between them.
    constexpr const char *const MATCH_SCORE_LABEL_SETS = "SETY";
    constexpr const char *const MATCH_SCORE_LABEL_GEMS = "GEMY";
    constexpr const char *const MATCH_SCORE_LABEL_TIEBREAK = "TIEBREAK";

    // Roster editor (V2). PROFILE, not GRACZE: the in-match player picker is already
    // called GRACZE, and two screens under one word is how you open the wrong one.
    // The e-paper placard is a pre-rendered bitmap and cannot read this table - its
    // wording lives in helpers/player_setup_qr.py and has to be changed there too.
    // The web page itself is out of scope, like the WiFi config
    // page; only what the device's own screens say lives here. The OLED small font
    // fits 9 characters per line.
    constexpr const char *const PLAYER_SETUP_OLED_TITLE = "PROFILE";
    constexpr const char *const PLAYER_SETUP_OLED_USE_EINK =   "Zobacz drugi";
    constexpr const char *const PLAYER_SETUP_OLED_USE_EINK_2 = "  ekran";

    // Overlay
    constexpr const char *const OVERLAY_LOW_BATTERY_TITLE = "NISKA BATERIA";

    // Boot / WiFi, OLED only
    constexpr const char *const BOOT_OLED_INITIALIZING = "STARTUJE...";
    constexpr const char *const BOOT_OLED_WIFI_CONNECTING_PREFIX = "WIFI: ";
    constexpr const char *const BOOT_OLED_CREDENTIALS_SAVED = "ZAPISANO. RESTART...";

    // LED words - 4 characters, spelled as they light up (see LedText.h).
    // LED_CONFIG_BRIGHTNESS is 3: the view appends the level digit.
    constexpr const char *const LED_CONFIG_BRIGHTNESS = "LEd";
    constexpr const char *const LED_CONFIG_BUZZER = "buZZ";
    constexpr const char *const LED_CONFIG_WIFI = "SIEC";
    constexpr const char *const LED_CONFIG_REBOOT = "rESt";
    constexpr const char *const LED_CONFIG_RETURN = "CoFn";
    constexpr const char *const LED_MODE_SQUASH = "S0UA";
    constexpr const char *const LED_MODE_VOLLEYBALL = "SIAt";
    constexpr const char *const LED_MODE_SHORT_VOLLEYBALL = "SIA1";
    constexpr const char *const LED_MODE_PADEL = "PAdE";
    constexpr const char *const LED_MODE_CONFIG = "oPCJ";
    constexpr const char *const LED_MODE_PLAYERS = "ProF";
    constexpr const char *const LED_PLAYER_SETUP = "ProF";
    constexpr const char *const LED_PLAYERS_START = "StAr";
    constexpr const char *const LED_OVERLAY_LOW_BATTERY = "bAtt";

#else

    // CONFIG menu
    constexpr const char *const CONFIG_MENU_TITLE = "CONFIG";
    constexpr const char *const CONFIG_ROW_BRIGHTNESS_LABEL = "Bright";
    constexpr const char *const CONFIG_ROW_BUZZER_LABEL = "Buzzer";
    constexpr const char *const CONFIG_ROW_WIFI_LABEL = "WiFi";
    constexpr const char *const CONFIG_ROW_REBOOT_LABEL = "Reboot";
    constexpr const char *const CONFIG_ROW_RETURN_LABEL = "Return";
    constexpr const char *const CONFIG_VALUE_ON = "ON";
    constexpr const char *const CONFIG_VALUE_OFF = "OFF";
    constexpr const char *const CONFIG_OPTION_BRIGHTNESS_OLED = "Brightness";
    constexpr const char *const CONFIG_OPTION_BUZZER_OLED = "Buzzer";
    constexpr const char *const CONFIG_OPTION_WIFI_OLED = "WiFi";
    constexpr const char *const CONFIG_OPTION_REBOOT_OLED = " [Reboot]";
    constexpr const char *const CONFIG_OPTION_RETURN_OLED = " [Return]";

    // Mode selector
    constexpr const char *const MODE_MENU_TITLE = "MODE";
    constexpr const char *const MODE_OPTION_SQUASH = "Squash";
    constexpr const char *const MODE_OPTION_VOLLEYBALL = "Volleyball";
    constexpr const char *const MODE_OPTION_SHORT_VOLLEYBALL = "Volley 15";
    constexpr const char *const MODE_OPTION_PADEL = "Padel";
    constexpr const char *const MODE_OPTION_CONFIG = "Config";
    constexpr const char *const MODE_OPTION_SQUASH_OLED = "  Squash  ";
    constexpr const char *const MODE_OPTION_VOLLEYBALL_OLED = "Volleyball";
    constexpr const char *const MODE_OPTION_SHORT_VOLLEYBALL_OLED = "Volleyb 15";
    constexpr const char *const MODE_OPTION_PADEL_OLED = "  Padel  ";
    constexpr const char *const MODE_OPTION_PLAYERS = "Profiles";
    constexpr const char *const MODE_OPTION_PLAYERS_OLED = "[Profiles]";
    constexpr const char *const MODE_OPTION_CONFIG_OLED = " [Config] ";

    // Player picker
    constexpr const char *const PLAYERS_ROW_START = "Start";
    constexpr const char *const PLAYERS_OPTION_START_OLED = " [Start] ";
    constexpr const char *const PLAYERS_FOOTER_COUNT_FMT = "%u in";

    // E-paper match score labels
    constexpr const char *const MATCH_SCORE_LABEL_SETS = "SETS";
    constexpr const char *const MATCH_SCORE_LABEL_GEMS = "GEMS";
    constexpr const char *const MATCH_SCORE_LABEL_TIEBREAK = "TIE";

    // Roster editor (V2)
    constexpr const char *const PLAYER_SETUP_OLED_TITLE = "PROFILES";
    constexpr const char *const PLAYER_SETUP_OLED_USE_EINK =   " Use front";
    constexpr const char *const PLAYER_SETUP_OLED_USE_EINK_2 = "  screen";


    // Overlay
    constexpr const char *const OVERLAY_LOW_BATTERY_TITLE = "LOW BATTERY";

    // Boot / WiFi, OLED only
    constexpr const char *const BOOT_OLED_INITIALIZING = "Initializing...";
    constexpr const char *const BOOT_OLED_WIFI_CONNECTING_PREFIX = "Connecting to ";
    constexpr const char *const BOOT_OLED_CREDENTIALS_SAVED = "Credentials saved! Rebooting...";

    // LED words - 4 characters, spelled as they light up (see LedText.h).
    // LED_CONFIG_BRIGHTNESS is short: the view appends the level digit.
    constexpr const char *const LED_CONFIG_BRIGHTNESS = "br";
    constexpr const char *const LED_CONFIG_BUZZER = "buZZ";
    constexpr const char *const LED_CONFIG_WIFI = "conn";
    constexpr const char *const LED_CONFIG_REBOOT = "boot";
    constexpr const char *const LED_CONFIG_RETURN = "rEtu";
    constexpr const char *const LED_MODE_SQUASH = "S0UA";
    constexpr const char *const LED_MODE_VOLLEYBALL = "bALL";
    constexpr const char *const LED_MODE_SHORT_VOLLEYBALL = "Shor";
    constexpr const char *const LED_MODE_PADEL = "PAdE";
    constexpr const char *const LED_MODE_CONFIG = "CFG";
    constexpr const char *const LED_MODE_PLAYERS = "ProF";
    constexpr const char *const LED_PLAYER_SETUP = "ProF";
    constexpr const char *const LED_PLAYERS_START = "PLAY";
    constexpr const char *const LED_OVERLAY_LOW_BATTERY = "bAtt";

#endif
}

#endif //STRINGS_H
