#include <version.h>

#include <Update.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

#include "../lib/RemoteDevelopmentService/RemoteDevelopmentService.h"
#include "../lib/RemoteInput/RemoteInputManager.h"

// OLED SSD1106
constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;
constexpr uint8_t OLED_RESET = -1;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// 433Mhz Receiver
constexpr uint8_t REMOTE_GPIO_D0 = 8;
constexpr uint8_t REMOTE_GPIO_D1 = 10;
constexpr uint8_t REMOTE_GPIO_D2 = 13;
constexpr uint8_t REMOTE_GPIO_D3 = 14;

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

    if (triggeredGpio) {
        remoteInputManager.trigger(triggeredGpio);
        triggeredGpio = 0;
    }

    if (remoteInputManager.buttonA.takeActionIfPossible()) {
        pixels.fill(Adafruit_NeoPixel::Color(5, 0, 0));
        pixels.show();
    }

    if (remoteInputManager.buttonB.takeActionIfPossible()) {
        pixels.fill(Adafruit_NeoPixel::Color(0, 5, 0));
        pixels.show();
    }
}
