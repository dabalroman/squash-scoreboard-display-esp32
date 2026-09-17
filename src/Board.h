#ifndef BOARD_H
#define BOARD_H

#include <Arduino.h>

/**
 * The only place pins and board facts are switched. BOARD_REV comes from
 * platformio.ini. `#if BOARD_REV` belongs here and in hardware wrapper headers
 * only - never in views, modes, Tournament, Match, Game or Rules.
 */

#if !defined(BOARD_REV)
#  error "Set -DBOARD_REV=1 (S2 Mini) or 2 (S3 DevKitC N16R8) in platformio.ini"
#elif BOARD_REV == 1
#  if !CONFIG_IDF_TARGET_ESP32S2
#    error "BOARD_REV=1 is the ESP32-S2 build"
#  endif

// V1 - Wemos S2 Mini, 112-LED 7-segment scoreboard.
namespace Board {
    constexpr const char *NAME = "V1 ESP32-S2";
    constexpr bool SERIAL_LOG = false;   // logs go to telnet only

    constexpr uint8_t OLED_SDA = 33;
    constexpr uint8_t OLED_SCL = 34;

    constexpr uint8_t RF_D0 = 14;   // button A
    constexpr uint8_t RF_D1 = 13;   // button B
    constexpr uint8_t RF_D2 = 10;   // button C
    constexpr uint8_t RF_D3 = 8;    // button D

    constexpr uint8_t LED_DATA = 18;
    constexpr uint8_t BUZZER = 3;

    constexpr uint16_t LED_COUNT = 112;   // 88 glyph/colon/indicator px + 24 bar px
}

#elif BOARD_REV == 2
#  if !CONFIG_IDF_TARGET_ESP32S3
#    error "BOARD_REV=2 is the ESP32-S3 build"
#  endif

// V2 - ESP32-S3-DevKitC-1 N16R8. Source of truth: the V2 KiCad schematic.
namespace Board {
    constexpr const char *NAME = "V2 ESP32-S3";
    constexpr bool SERIAL_LOG = true;    // mirror logs to native USB CDC when a host is attached

    constexpr uint8_t OLED_SDA = 4;    // V1: 33 (33-37 are octal PSRAM here)
    constexpr uint8_t OLED_SCL = 5;    // V1: 34

    constexpr uint8_t RF_D0 = 14;      // button A, same as V1
    constexpr uint8_t RF_D1 = 13;      // button B
    constexpr uint8_t RF_D2 = 10;      // button C
    constexpr uint8_t RF_D3 = 8;       // button D

    constexpr uint8_t LED_DATA = 18;   // same as V1
    constexpr uint8_t BUZZER = 3;      // same as V1, now via MOSFET

    constexpr uint8_t EINK_SCK = 12;   // WeAct silk says SCL
    constexpr uint8_t EINK_MOSI = 11;  // WeAct silk says SDA
    constexpr uint8_t EINK_CS = 9;
    constexpr uint8_t EINK_DC = 15;
    constexpr uint8_t EINK_RST = 16;
    constexpr uint8_t EINK_BUSY = 17;

    constexpr uint8_t BATTERY_ADC = 6; // ADC1_CH5, BAT+ through a 10k/10k divider

    // LED chain, in slots (94 LEDs fitted; digit modules share slots).
    // 0-9 centre block (border + indicators), then four 16-slot digit modules.
    constexpr uint16_t LED_COUNT = 74;
    constexpr uint16_t DIGIT_BASE = 10;
    constexpr uint16_t PIXELS_PER_MODULE = 16;
    constexpr uint8_t MODULE_COUNT = 4;

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
static_assert(Board::DIGIT_BASE + Board::MODULE_COUNT * Board::PIXELS_PER_MODULE <= Board::LED_COUNT,
              "digit modules exceed the LED buffer");

#else
#  error "Unknown BOARD_REV"
#endif

#endif //BOARD_H
