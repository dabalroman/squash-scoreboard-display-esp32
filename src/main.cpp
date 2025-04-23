#include <version.h>

#include <Arduino.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FastLED.h>
#include <time.h>

#include "UserProfile.h"
#include "Display/GlyphDisplay.h"
#include "../lib/RemoteDevelopmentService/RemoteDevelopmentService.h"
#include "../lib/RemoteInput/RemoteInputManager.h"
#include "Match/Tournament.h"
#include "Match/Rules/SquashRules.h"

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
CRGB pixels[LED_WS2812B_AMOUNT];

Preferences preferences;
RemoteDevelopmentService developmentService(preferences);

volatile uint8_t interruptTriggeredGpio = 0;
void IRAM_ATTR onRemoteReceiverInterrupt_d0() { interruptTriggeredGpio = REMOTE_RECEIVER_GPIO_D0; }
void IRAM_ATTR onRemoteReceiverInterrupt_d1() { interruptTriggeredGpio = REMOTE_RECEIVER_GPIO_D1; }
void IRAM_ATTR onRemoteReceiverInterrupt_d2() { interruptTriggeredGpio = REMOTE_RECEIVER_GPIO_D2; }
void IRAM_ATTR onRemoteReceiverInterrupt_d3() { interruptTriggeredGpio = REMOTE_RECEIVER_GPIO_D3; }

uint16_t offset = 2137;
unsigned long lastUpdate = 0;

uint8_t digitPartOffset = 0;
GlyphDisplay ledDisplay(pixels);


UserProfile userA(0, "Andrzej", {255, 0, 0});
UserProfile userB(1, "Bartosz", {0, 255, 0});
UserProfile userC(2, "Cecil", {0, 0, 255});

SquashRules squashRules;
Tournament tournament(squashRules);

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
    display.println("Initializing...");
    display.display();

    FastLED.addLeds<NEOPIXEL, LED_WS2812B_GPIO>(pixels, LED_WS2812B_AMOUNT);
    FastLED.setBrightness(10);
    FastLED.setMaxRefreshRate(400);
    FastLED.clear();
    FastLED.show();

    attachInterrupt(digitalPinToInterrupt(REMOTE_RECEIVER_GPIO_D0), onRemoteReceiverInterrupt_d0, RISING);
    attachInterrupt(digitalPinToInterrupt(REMOTE_RECEIVER_GPIO_D1), onRemoteReceiverInterrupt_d1, RISING);
    attachInterrupt(digitalPinToInterrupt(REMOTE_RECEIVER_GPIO_D2), onRemoteReceiverInterrupt_d2, RISING);
    attachInterrupt(digitalPinToInterrupt(REMOTE_RECEIVER_GPIO_D3), onRemoteReceiverInterrupt_d3, RISING);
}

void setup() {
    initHardware();
    developmentService.init(display);

    developmentService.printLn("ESP-S2 ready. FW version: %s, %s %s\n", FW_VERSION, __DATE__, __TIME__);

    Match &match = tournament.getMatchBetween(userA, userB);
    tournament.setActiveMatch(match);

    const UserProfile &userA = match.getUserAProfile();
    const UserProfile &userB = match.getUserBProfile();

    MatchRound &round = tournament.getActiveMatch().getActiveRound();
    round.scorePointA();
    round.scorePointB();
    round.commit();
    developmentService.printLn("%s: %d vs %s: %d", userA.getName(), round.getScoreA(), userB.getName(), round.getScoreB());

    round.scorePointA();
    round.commit();
    developmentService.printLn("%s: %d vs %s: %d", userA.getName(), round.getScoreA(), userB.getName(), round.getScoreB());

    round.scorePointB();
    developmentService.printLn("%s: %d vs %s: %d", userA.getName(), round.getScoreA(), userB.getName(), round.getScoreB());

    round.rollback();
    round.commit();
    developmentService.printLn("%s: %d vs %s: %d", userA.getName(), round.getScoreA(), userB.getName(), round.getScoreB());

    round.scorePointB();
    round.scorePointB();
    round.scorePointB();
    round.scorePointB();
    round.scorePointB();
    round.scorePointB();
    round.scorePointB();
    round.scorePointB();
    round.scorePointB();
    round.scorePointB();
    round.commit();
    developmentService.printLn("%s: %d vs %s: %d", userA.getName(), round.getScoreA(), userB.getName(), round.getScoreB());

    round.scorePointA();
    round.commit();
    developmentService.printLn("%s: %d vs %s: %d", userA.getName(), round.getScoreA(), userB.getName(), round.getScoreB());

    developmentService.printLn("%d", round.getWinner());
}

void loop() {
    developmentService.loop();
    remoteInputManager.handleInput(interruptTriggeredGpio);

    if (lastUpdate + 1000 < millis()) {
        lastUpdate = millis();

        FastLED.clear();

        time_t now = time(nullptr);
        tm *timeinfo = localtime(&now);
        ledDisplay.setValue(timeinfo->tm_hour, timeinfo->tm_min);
        ledDisplay.toggleColon();

        ledDisplay.show();
        FastLED.show();
    }

    if (remoteInputManager.buttonA.takeActionIfPossible()) {
        offset++;
        developmentService.printLn("%d", offset);
    }

    if (remoteInputManager.buttonB.takeActionIfPossible()) {
        offset += 100;
        developmentService.printLn("%d", offset);
    }

    if (remoteInputManager.buttonC.takeActionIfPossible()) {
        offset--;
        developmentService.printLn("%d", offset);
    }

    if (remoteInputManager.buttonD.takeActionIfPossible()) {
        offset -= 100;
        developmentService.printLn("%d", offset);
    }
}
