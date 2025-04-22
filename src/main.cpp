#include <version.h>

#include <Update.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

#include "../lib/RemoteDevelopmentService/RemoteDevelopmentService.h"
#include "../lib/RemoteInput/RemoteInputManager.h"

constexpr uint8_t OLED_SSD1106_SCREEN_WIDTH = 128;
constexpr uint8_t OLED_SSD1106_SCREEN_HEIGHT = 64;
constexpr uint8_t OLED_SSD1106_I2C_SDA_GPIO = 33;
constexpr uint8_t OLED_SSD1106_I2C_SCL_GPIO = 34;
constexpr uint8_t OLED_SSD1106_I2C_ADDRESS = 0x3C;
Adafruit_SSD1306 display(OLED_SSD1106_SCREEN_WIDTH, OLED_SSD1106_SCREEN_HEIGHT, &Wire);

constexpr uint8_t REMOTE_RECEIVER_GPIO_D0 = 14;
constexpr uint8_t REMOTE_RECEIVER_GPIO_D1 = 13;
constexpr uint8_t REMOTE_RECEIVER_GPIO_D2 = 10;
constexpr uint8_t REMOTE_RECEIVER_GPIO_D3 = 8;
RemoteInputManager remoteInputManager(
    REMOTE_RECEIVER_GPIO_D0,
    REMOTE_RECEIVER_GPIO_D1,
    REMOTE_RECEIVER_GPIO_D2,
    REMOTE_RECEIVER_GPIO_D3
);

constexpr uint8_t LED_WS2812B_GPIO = 18;
constexpr uint8_t LED_WS2812B_AMOUNT = 86;
Adafruit_NeoPixel pixels(LED_WS2812B_AMOUNT, LED_WS2812B_GPIO, NEO_GRB + NEO_KHZ800);

RemoteDevelopmentService developmentService;

volatile uint8_t interruptTriggeredGpio = 0;
void IRAM_ATTR onRemoteReceiverInterrupt_d0() { interruptTriggeredGpio = REMOTE_RECEIVER_GPIO_D0; }
void IRAM_ATTR onRemoteReceiverInterrupt_d1() { interruptTriggeredGpio = REMOTE_RECEIVER_GPIO_D1; }
void IRAM_ATTR onRemoteReceiverInterrupt_d2() { interruptTriggeredGpio = REMOTE_RECEIVER_GPIO_D2; }
void IRAM_ATTR onRemoteReceiverInterrupt_d3() { interruptTriggeredGpio = REMOTE_RECEIVER_GPIO_D3; }

uint8_t offset = 0;
unsigned long lastUpdate = 0;

uint8_t digitPartOffset = 0;

void initHardware() {
    Wire.begin(OLED_SSD1106_I2C_SDA_GPIO, OLED_SSD1106_I2C_SCL_GPIO);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_SSD1106_I2C_ADDRESS)) {
        developmentService.printLn("SSD1106 allocation failed!");
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

    attachInterrupt(digitalPinToInterrupt(REMOTE_RECEIVER_GPIO_D0), onRemoteReceiverInterrupt_d0, RISING);
    attachInterrupt(digitalPinToInterrupt(REMOTE_RECEIVER_GPIO_D1), onRemoteReceiverInterrupt_d1, RISING);
    attachInterrupt(digitalPinToInterrupt(REMOTE_RECEIVER_GPIO_D2), onRemoteReceiverInterrupt_d2, RISING);
    attachInterrupt(digitalPinToInterrupt(REMOTE_RECEIVER_GPIO_D3), onRemoteReceiverInterrupt_d3, RISING);
}

void setup() {
    developmentService.init();
    initHardware();

    developmentService.printLn("ESP-S2 ready. FW version: %s, %s %s\n", FW_VERSION, __DATE__, __TIME__);
}

void loop() {
    developmentService.loop();
    remoteInputManager.handleInput(interruptTriggeredGpio);

    if (lastUpdate + 50 < millis()) {
        lastUpdate = millis();

        pixels.fill(Adafruit_NeoPixel::Color(0, 0, 0));
        pixels.setPixelColor(offset, Adafruit_NeoPixel::Color(1, 1, 1));

        pixels.show();
    }

    if (remoteInputManager.buttonA.takeActionIfPossible()) {
        offset++;
        developmentService.printLn("%d", offset);
    }

    if (remoteInputManager.buttonB.takeActionIfPossible()) {
        offset--;
        developmentService.printLn("%d", offset);
    }

    if (remoteInputManager.buttonC.takeActionIfPossible()) {
        offset += 3;
        developmentService.printLn("%d", offset);
    }

    if (remoteInputManager.buttonD.takeActionIfPossible()) {
        offset -= 3;
        developmentService.printLn("%d", offset);
    }
}
