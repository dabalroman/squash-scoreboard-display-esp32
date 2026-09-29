#ifndef BOARD_H
#define BOARD_H

#include <Arduino.h>

/**
 * The only place pins and board facts are switched. BOARD_REV comes from
 * platformio.ini. `#if BOARD_REV` belongs here and in hardware wrapper headers
 * only - never in views, modes, Tournament, Match, Game or Rules.
 */

#if !CONFIG_IDF_TARGET_ESP32S3
#  error "Both boards are an ESP32-S3-DevKitC-1 N16R8"
#endif

// Shared by both boards: same module, same wiring. V1 leaves the e-paper pins
// unconnected; the battery divider is fitted on both.
namespace Board {
    constexpr uint8_t OLED_SDA = 4;    // 33-37 are octal PSRAM
    constexpr uint8_t OLED_SCL = 5;

    // The remote's prev/next/undo/enter (verified on the device 2026-09-17).
    constexpr uint8_t RF_D0 = 8;       // button A - prev
    constexpr uint8_t RF_D1 = 10;      // button B - next
    constexpr uint8_t RF_D2 = 13;      // button C - undo
    constexpr uint8_t RF_D3 = 14;      // button D - enter

    constexpr uint8_t LED_DATA = 18;
    constexpr uint8_t BUZZER = 3;      // via MOSFET on V2

    constexpr uint8_t EINK_SCK = 12;   // WeAct silk says SCL
    constexpr uint8_t EINK_MOSI = 11;  // WeAct silk says SDA
    constexpr uint8_t EINK_CS = 9;
    constexpr uint8_t EINK_DC = 15;
    constexpr uint8_t EINK_RST = 16;
    constexpr uint8_t EINK_BUSY = 17;

    constexpr uint8_t BATTERY_ADC = 6; // ADC1_CH5, BAT+ through a 10k/10k divider

    // GPIO 26-32 are SPI flash and 33-37 octal PSRAM on an N16R8.
    constexpr bool pinIsSafe(const uint8_t pin) {
        return !(pin >= 26 && pin <= 37);
    }
}

static_assert(Board::pinIsSafe(Board::OLED_SDA) && Board::pinIsSafe(Board::OLED_SCL), "OLED pins hit flash/PSRAM");
static_assert(Board::pinIsSafe(Board::RF_D0) && Board::pinIsSafe(Board::RF_D1)
              && Board::pinIsSafe(Board::RF_D2) && Board::pinIsSafe(Board::RF_D3), "RF pins hit flash/PSRAM");
static_assert(Board::pinIsSafe(Board::LED_DATA) && Board::pinIsSafe(Board::BUZZER), "LED/buzzer pins hit flash/PSRAM");
static_assert(Board::pinIsSafe(Board::EINK_SCK) && Board::pinIsSafe(Board::EINK_MOSI)
              && Board::pinIsSafe(Board::EINK_CS) && Board::pinIsSafe(Board::EINK_DC)
              && Board::pinIsSafe(Board::EINK_RST) && Board::pinIsSafe(Board::EINK_BUSY), "E-ink pins hit flash/PSRAM");
static_assert(Board::pinIsSafe(Board::BATTERY_ADC), "Battery ADC pin hits flash/PSRAM");

#if !defined(BOARD_REV)
#  error "Set -DBOARD_REV=1 (V1, 7-segment) or 2 (V2, 9-segment + e-paper) in platformio.ini"
#elif BOARD_REV == 1

// V1 - 112-LED 7-segment scoreboard.
namespace Board {
    constexpr const char *NAME = "V1 ESP32-S3";

    // The roster editor: shared with V2 (PlayerSetupWebUi carries no #if). No e-paper
    // here, so no QR placard - the OLED discovery screen (AP name, password, IP) is
    // what V1 has instead.
    constexpr bool HAS_PLAYER_SETUP = true;

    constexpr uint8_t OLED_ROTATION = 2;   // mounted upside down

    // Healthy panel: text may use every row (BackDisplay::DEAD_TOP_ROWS).
    constexpr uint8_t OLED_DEAD_TOP_ROWS = 0;

    // Divider x ADC calibration. Starting value copied from V2 - same 10k/10k
    // divider and module, but not yet measured against a meter on this unit.
    constexpr float BATTERY_FACTOR = 2.027f;

    constexpr uint16_t LED_COUNT = 112;   // 88 glyph/colon/indicator px + 24 bar px

    // Power-draw limit (user-confirmed, added 2026-09-19 in 3aaa578). V1 has no
    // nine-segment modules, so #43's per-segment compensation does not apply here -
    // this stays the permanent guard.
    constexpr float GLOBAL_BRIGHTNESS_SCALE = 0.8f;
}

#elif BOARD_REV == 2

// V2 - 9-segment + border + e-paper. Source of truth: the V2 KiCad schematic.
namespace Board {
    constexpr const char *NAME = "V2 ESP32-S3";

    // The roster editor: an AP, a web page and the dual-QR placard on the e-paper.
    constexpr bool HAS_PLAYER_SETUP = true;

    constexpr uint8_t OLED_ROTATION = 0;   // mounted upright (device, 2026-09-17)

    // The bench panel is damaged: every even row from 0 to 12 is dead. 11 is the
    // first row text may occupy (BackDisplay::DEAD_TOP_ROWS has the reasoning).
    constexpr uint8_t OLED_DEAD_TOP_ROWS = 11;

    // Combined divider (measured 1.988) x ADC calibration, measured on core 2.0.17
    // against a meter (V2 Guidelines, Part 2, Battery sense). Never hard-code x2.
    constexpr float BATTERY_FACTOR = 2.027f;

    // LED chain, in slots (94 LEDs fitted; digit modules share slots).
    // 0-9 centre block (border + indicators), then four 16-slot digit modules.
    constexpr uint16_t LED_COUNT = 74;
    constexpr uint16_t DIGIT_BASE = 10;
    constexpr uint16_t PIXELS_PER_MODULE = 16;
    constexpr uint8_t MODULE_COUNT = 4;

    // #43's per-segment compensation (NineSegmentBrightness) is V2's power guard now -
    // worst case 69.6 die-equivalents vs 72 under the old flat 0.8 limit, so peak draw
    // does not rise. No blanket cut needed on top of it.
    constexpr float GLOBAL_BRIGHTNESS_SCALE = 1.0f;
}

static_assert(Board::DIGIT_BASE + Board::MODULE_COUNT * Board::PIXELS_PER_MODULE <= Board::LED_COUNT,
              "digit modules exceed the LED buffer");

#else
#  error "Unknown BOARD_REV"
#endif

#endif //BOARD_H
