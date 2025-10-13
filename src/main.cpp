#include <version.h>

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FastLED.h>

#include "DeviceMode/DeviceModeState.h"
#include "UserProfile.h"
#include "Display/GlyphDisplay.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/ConfigMode/ConfigMode.h"
#include "DeviceMode/SquashMode/SquashMode.h"
#include "RemoteInput/RemoteInputManager.h"
#include "RemoteDevelopmentService/RemoteDevelopmentService.h"
#include "RemoteDevelopmentService/LoggerHelper.h"

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
constexpr uint8_t LED_WS2812B_AMOUNT = 88;
CRGB pixels[LED_WS2812B_AMOUNT];
GlyphDisplay ledDisplay(pixels);

// Need to wait for I2C init
BackDisplay *backDisplay = nullptr;

RemoteDevelopmentService *gRemoteDevelopmentService = nullptr;
PreferencesManager preferencesManager;

volatile uint8_t interruptTriggeredGpio = 0;
void IRAM_ATTR onRemoteReceiverInterrupt_d0() { interruptTriggeredGpio = REMOTE_RECEIVER_GPIO_D0; }
void IRAM_ATTR onRemoteReceiverInterrupt_d1() { interruptTriggeredGpio = REMOTE_RECEIVER_GPIO_D1; }
void IRAM_ATTR onRemoteReceiverInterrupt_d2() { interruptTriggeredGpio = REMOTE_RECEIVER_GPIO_D2; }
void IRAM_ATTR onRemoteReceiverInterrupt_d3() { interruptTriggeredGpio = REMOTE_RECEIVER_GPIO_D3; }

unsigned long lastUpdate = 0;

DeviceMode *deviceMode = nullptr;
DeviceModeState deviceState = DeviceModeState::Booting;

UserProfile userA(0, "Adrian", Colors::Green);
UserProfile userB(1, "Roman", Colors::Yellow);
UserProfile userC(2, "Basia", Colors::Pink);
UserProfile userD(3, "Krystian", Colors::Blue);
UserProfile userE(4, "Jola", Colors::Red);
UserProfile userF(5, "Cegiel", Colors::White);
UserProfile userG(6, "Szymon", Colors::Pink);
UserProfile userH(7, "Igor", Colors::Green);
UserProfile userI(8, "Damian", Colors::White);

std::vector<UserProfile *> users = {&userA, &userB, &userC, &userD, &userE, &userF, &userG, &userH, &userI};

void initHardware() {
    Wire.begin(OLED_SSD1106_I2C_SDA_GPIO, OLED_SSD1106_I2C_SCL_GPIO);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_SSD1106_I2C_ADDRESS)) {
        printLn("SSD1106 allocation failed!");
    }

    backDisplay = new BackDisplay(&display);

    FastLED.addLeds<NEOPIXEL, LED_WS2812B_GPIO>(pixels, LED_WS2812B_AMOUNT);
    FastLED.setBrightness(preferencesManager.settings.brightness);
    FastLED.setMaxRefreshRate(400);
    FastLED.clear();
    FastLED.show();

    attachInterrupt(digitalPinToInterrupt(REMOTE_RECEIVER_GPIO_D0), onRemoteReceiverInterrupt_d0, RISING);
    attachInterrupt(digitalPinToInterrupt(REMOTE_RECEIVER_GPIO_D1), onRemoteReceiverInterrupt_d1, RISING);
    attachInterrupt(digitalPinToInterrupt(REMOTE_RECEIVER_GPIO_D2), onRemoteReceiverInterrupt_d2, RISING);
    attachInterrupt(digitalPinToInterrupt(REMOTE_RECEIVER_GPIO_D3), onRemoteReceiverInterrupt_d3, RISING);
}

void changeDeviceMode(const DeviceModeState deviceModeState) {
    deviceState = deviceModeState;

    switch (deviceModeState) {
        case DeviceModeState::ConfigMode:
            deviceMode = new ConfigMode(
                ledDisplay,
                *backDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { changeDeviceMode(state); },
                preferencesManager
            );
            break;
        case DeviceModeState::SquashMode:
            deviceMode = new SquashMode(
                ledDisplay,
                *backDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { changeDeviceMode(state); },
                users
            );
            break;
        default:
            break;
    }
}

void setup() {
    preferencesManager.read();
    initHardware();

    static RemoteDevelopmentService remoteDev;
    remoteDev.init(preferencesManager, *backDisplay);
    gRemoteDevelopmentService = &remoteDev;

    printLn("ESP-S2 ready. FW version: %s, %s %s\n", FW_VERSION, __DATE__, __TIME__);
    printLn("Read from config:");
    printLn("  enableAp: %d", preferencesManager.settings.enableAp);
    printLn("  enableWifi: %d", preferencesManager.settings.enableWifi);
    printLn("  brightness: %d", preferencesManager.settings.brightness);
    printLn("  wifiSSID: %s", preferencesManager.settings.wifiSSID);
    printLn("  wifiPassword: %s", preferencesManager.settings.wifiPassword);

    changeDeviceMode(DeviceModeState::SquashMode);
}

void loop() {
    gRemoteDevelopmentService->loop();
    remoteInputManager.handleInput(interruptTriggeredGpio);

    // At most 20 fps, for now
    if (lastUpdate + 50 > millis()) {
        return;
    }

    lastUpdate = millis();

    if (deviceMode) {
        deviceMode->loop();
    }

    // if (lastUpdate + 1000 < millis()) {
    //     lastUpdate = millis();
    //
    //     time_t now = time(nullptr);
    //     tm *timeinfo = localtime(&now);
    //     ledDisplay.setValue(timeinfo->tm_hour, timeinfo->tm_min);
    //     ledDisplay.toggleColon();
    //
    //     ledDisplay.clear();
    //     ledDisplay.render();
    //     ledDisplay.show();
    // }
}
