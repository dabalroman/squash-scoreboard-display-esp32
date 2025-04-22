#include <version.h>

#include <Update.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

#include "DisplayDigit.h"
#include "../lib/RemoteDevelopmentService/RemoteDevelopmentService.h"
#include "../lib/RemoteInput/RemoteInputManager.h"

// OLED SSD1106
constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;
constexpr uint8_t OLED_RESET = -1;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// 433Mhz Receiver
constexpr uint8_t REMOTE_GPIO_D0 = 14;
constexpr uint8_t REMOTE_GPIO_D1 = 13;
constexpr uint8_t REMOTE_GPIO_D2 = 10;
constexpr uint8_t REMOTE_GPIO_D3 = 8;

// Neopixel
constexpr uint8_t LED_PIN = 18;
constexpr uint8_t NUM_LEDS = 86;
Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

RemoteDevelopmentService developmentService;
RemoteInputManager remoteInputManager(REMOTE_GPIO_D0, REMOTE_GPIO_D1, REMOTE_GPIO_D2, REMOTE_GPIO_D3);

volatile uint8_t triggeredGpio = 0;

void IRAM_ATTR onGpioInterrupt0() { triggeredGpio = REMOTE_GPIO_D0; }
void IRAM_ATTR onGpioInterrupt1() { triggeredGpio = REMOTE_GPIO_D1; }
void IRAM_ATTR onGpioInterrupt2() { triggeredGpio = REMOTE_GPIO_D2; }
void IRAM_ATTR onGpioInterrupt3() { triggeredGpio = REMOTE_GPIO_D3; }

uint8_t offset = 0;
unsigned long lastUpdate = 0;

uint8_t digitPartOffset = 0;

void setup() {
    developmentService.init();

    attachInterrupt(digitalPinToInterrupt(REMOTE_GPIO_D0), onGpioInterrupt0, RISING);
    attachInterrupt(digitalPinToInterrupt(REMOTE_GPIO_D1), onGpioInterrupt1, RISING);
    attachInterrupt(digitalPinToInterrupt(REMOTE_GPIO_D2), onGpioInterrupt2, RISING);
    attachInterrupt(digitalPinToInterrupt(REMOTE_GPIO_D3), onGpioInterrupt3, RISING);

    Wire.begin(33, 34);

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        developmentService.telnetPrintLn("SSD1106 allocation failed!");
    }

    display.setRotation(2);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(WiFi.localIP().toString().c_str());
    display.display();

    pixels.begin();
    pixels.show();

    developmentService.telnetPrintLn("ESP-S2 ready. FW version: %s, %s %s\n", FW_VERSION, __DATE__, __TIME__);
}

void loop() {
    developmentService.handle();

    if (lastUpdate + 50 < millis()) {
        lastUpdate = millis();

        // for (uint16_t i = 0; i < NUM_LEDS; ++i) {
            // if (i % 21 == 2) {
            //     pixels.setPixelColor(i, pixels.Color(1, 0, 0)); // Full white
            // } else if (i % 18 == 2) {
            //     pixels.setPixelColor(i, pixels.Color(1, 1, 0)); // Half white
            // } else if (i % 15 == 2) {
            //     pixels.setPixelColor(i, pixels.Color(1, 2, 0)); // Half white
            // } else if (i % 12 == 2) {
            //     pixels.setPixelColor(i, pixels.Color(1, 3, 0)); // Half white
            // } else if (i % 9 == 2) {
            //     pixels.setPixelColor(i, pixels.Color(1, 4, 0)); // Half white
            // } else if (i % 6 == 2) {
            //     pixels.setPixelColor(i, pixels.Color(1, 5, 0)); // Half white
            // } else if (i % 3 == 2) {
            //     pixels.setPixelColor(i, pixels.Color(1, 6, 0)); // Half white
            // } else {
                // if ((i + NUM_LEDS - offset) % NUM_LEDS == 0) {
                //     pixels.setPixelColor(i, pixels.Color(0, 1, 0));
                // } else {
                //     pixels.setPixelColor(i, pixels.Color(0, 0, 0));
                // }
            // }

            // for (uint8_t j = 0; j < 7; ++j) {
            // uint8_t j = digitPartOffset % 7;
            //     for (uint8_t k = 0; k < 3; ++k) {
            //         if (digitA.parts[j].pixelIndexes[k] == i) {
            //             pixels.setPixelColor(i, pixels.Color(1, 1, 0));
            //         }
            //         if (digitB.parts[j].pixelIndexes[k] == i) {
            //             pixels.setPixelColor(i, pixels.Color(1, 0, 1));
            //         }
            //
            //         if (digitC.parts[j].pixelIndexes[k] == i) {
            //             pixels.setPixelColor(i, pixels.Color(1, 0, 0));
            //         }
            //
            //         if (digitD.parts[j].pixelIndexes[k] == i) {
            //             pixels.setPixelColor(i, pixels.Color(0, 0, 1));
            //         }
            //     }
            // }

            pixels.fill(pixels.Color(0, 0, 0));
            pixels.setPixelColor(offset, pixels.Color(1, 1, 1));
        // }

        pixels.show();
    }

    if (triggeredGpio) {
        remoteInputManager.trigger(triggeredGpio);
        triggeredGpio = 0;
    }

    if (remoteInputManager.buttonA.takeActionIfPossible()) {
        offset++;
        developmentService.telnetPrintLn("%d", offset);
    }

    if (remoteInputManager.buttonB.takeActionIfPossible()) {
        offset--;
        developmentService.telnetPrintLn("%d", offset);
    }

    if (remoteInputManager.buttonC.takeActionIfPossible()) {
        offset+=3;
        developmentService.telnetPrintLn("%d", offset);
    }

    if (remoteInputManager.buttonD.takeActionIfPossible()) {
        offset-=3;
        developmentService.telnetPrintLn("%d", offset);
    }
}
