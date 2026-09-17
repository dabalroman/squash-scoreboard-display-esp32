#include <version.h>

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FastLED.h>

#include "Board.h"

#include "DeviceMode/DeviceModeState.h"
#include "UserProfile.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/LedStartupAnimation.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/ConfigMode/ConfigMode.h"
#include "DeviceMode/ModeSwitcherMode/ModeSwitchingMode.h"
#include "DeviceMode/SquashMode/SquashMode.h"
#include "DeviceMode/VolleyballMode/VolleyballMode.h"
#include "DeviceMode/PadelMode/PadelMode.h"
#include "Tournament/Rules/ShortVolleyballRules.h"
#include "Tournament/Rules/VolleyballRules.h"
#include "RemoteInput/RemoteInputManager.h"
#include "Buzzer.h"
#include "BatterySensor.h"
#include "BatteryMonitor.h"
#include "Display/Overlay.h"
#include "Display/LedDisplay/LedBar.h"
#include "Display/EInk/EInkDisplay.h"
#include "RemoteDevelopmentService/RemoteDevelopmentService.h"
#include "RemoteDevelopmentService/LoggerHelper.h"

constexpr uint8_t OLED_SSD1106_SCREEN_WIDTH = 128;
constexpr uint8_t OLED_SSD1106_SCREEN_HEIGHT = 64;
constexpr uint8_t OLED_SSD1106_I2C_ADDRESS = 0x3C;
Adafruit_SSD1306 display(OLED_SSD1106_SCREEN_WIDTH, OLED_SSD1106_SCREEN_HEIGHT, &Wire);

RemoteInputManager remoteInputManager(
    Board::RF_D0,
    Board::RF_D1,
    Board::RF_D2,
    Board::RF_D3
);

CRGB pixels[Board::LED_COUNT];
LedDisplay ledDisplay(pixels);

// Need to wait for I2C init
std::unique_ptr<BackDisplay> backDisplay;

Buzzer gBuzzer(Board::BUZZER);
BatterySensor batterySensor;
BatteryMonitor batteryMonitor(batterySensor);
unsigned long lastBatteryLogMs = 0;

// Device-level message that takes the displays over and pauses the active mode.
Overlay overlay;
bool lastBatteryLow = false;

// While the pack is low the LEDs are held at menu level 1, whatever the config says.
constexpr uint8_t LOW_BATTERY_BRIGHTNESS_CAP = 31;
constexpr uint32_t LOW_BATTERY_OVERLAY_MS = 10000;

// V2 e-paper; an empty stub on V1.
EInkDisplay einkDisplay;

RemoteDevelopmentService *gRemoteDevelopmentService = nullptr;
PreferencesManager preferencesManager;

volatile uint8_t interruptTriggeredGpio = 0;
void IRAM_ATTR onRemoteReceiverInterrupt_d0() { interruptTriggeredGpio = Board::RF_D0; }
void IRAM_ATTR onRemoteReceiverInterrupt_d1() { interruptTriggeredGpio = Board::RF_D1; }
void IRAM_ATTR onRemoteReceiverInterrupt_d2() { interruptTriggeredGpio = Board::RF_D2; }
void IRAM_ATTR onRemoteReceiverInterrupt_d3() { interruptTriggeredGpio = Board::RF_D3; }

unsigned long lastUpdate = 0;
unsigned long lastEInkStatsLog = 0;

std::unique_ptr<DeviceMode> deviceMode;
DeviceModeState deviceState = DeviceModeState::Booting;

UserProfile userA(0, "Adrian", Colors::Green);
UserProfile userB(1, "Roman", Colors::Yellow);
UserProfile userC(2, "Basia", Colors::Pink);
UserProfile userD(3, "Krystian", Colors::Blue);
UserProfile userE(4, "Jola", Colors::Red);
UserProfile userF(5, "Cegiel", Colors::White);
UserProfile userG(6, "Szymon", Colors::Aqua);
UserProfile userH(7, "Igor", Colors::Orange);
UserProfile userI(8, "Damian", Colors::Violet);

std::vector<UserProfile *> users = {&userA, &userB, &userC, &userD, &userE, &userF, &userG, &userH, &userI};

void initHardware() {
    Wire.begin(Board::OLED_SDA, Board::OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_SSD1106_I2C_ADDRESS)) {
        printLn("SSD1106 allocation failed!");
    }

    backDisplay = std::make_unique<BackDisplay>(&display);

    FastLED.addLeds<NEOPIXEL, Board::LED_DATA>(pixels, Board::LED_COUNT);
    // Through the wrapper, never FastLED directly: it applies the low-battery cap.
    ledDisplay.setBrightness(preferencesManager.settings.brightness);
    FastLED.setMaxRefreshRate(400);
    FastLED.clear();
    FastLED.show();

    attachInterrupt(digitalPinToInterrupt(Board::RF_D0), onRemoteReceiverInterrupt_d0, RISING);
    attachInterrupt(digitalPinToInterrupt(Board::RF_D1), onRemoteReceiverInterrupt_d1, RISING);
    attachInterrupt(digitalPinToInterrupt(Board::RF_D2), onRemoteReceiverInterrupt_d2, RISING);
    attachInterrupt(digitalPinToInterrupt(Board::RF_D3), onRemoteReceiverInterrupt_d3, RISING);
}

void changeDeviceMode(const DeviceModeState deviceModeState) {
    deviceState = deviceModeState;

    switch (deviceModeState) {
        default:
        case DeviceModeState::ModeSwitchingMode:
            deviceMode = std::make_unique<ModeSwitchingMode>(
                ledDisplay,
                *backDisplay,
                einkDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { changeDeviceMode(state); },
                batteryMonitor
            );
            break;

        case DeviceModeState::ConfigMode:
            deviceMode = std::make_unique<ConfigMode>(
                ledDisplay,
                *backDisplay,
                einkDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { changeDeviceMode(state); },
                preferencesManager,
                batteryMonitor
            );
            break;

        case DeviceModeState::SquashMode:
            deviceMode = std::make_unique<SquashMode>(
                ledDisplay,
                *backDisplay,
                einkDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { changeDeviceMode(state); },
                users,
                []{ gBuzzer.playCelebration(); }
            );
            break;

        case DeviceModeState::VolleyballMode:
            deviceMode = std::make_unique<VolleyballMode>(
                ledDisplay,
                *backDisplay,
                einkDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { changeDeviceMode(state); },
                users,
                std::make_unique<VolleyballRules>(),
                []{ gBuzzer.playCelebration(); }
            );
            break;

        case DeviceModeState::ShortVolleyballMode:
            deviceMode = std::make_unique<VolleyballMode>(
                ledDisplay,
                *backDisplay,
                einkDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { changeDeviceMode(state); },
                users,
                std::make_unique<ShortVolleyballRules>(),
                []{ gBuzzer.playCelebration(); }
            );
            break;

        case DeviceModeState::PadelMode:
            deviceMode = std::make_unique<PadelMode>(
                ledDisplay,
                *backDisplay,
                einkDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { changeDeviceMode(state); },
                users,
                []{ gBuzzer.playCelebration(); }
            );
            break;
    }
}

void setup() {
    // First thing on boot: drives the buzzer pad LOW and releases the hold safeRestart() set,
    // so the pin is never left floating while the rest of the hardware comes up.
    gBuzzer.init();

    if (Board::SERIAL_LOG) {
        Serial.begin(115200);
    }

    preferencesManager.read();
    initHardware();
    einkDisplay.begin();   // V2: blocks ~3 s once (initial full refresh), then the splash
    // begin() only queues the splash; the sweep below blocks before loop() can send it.
    einkDisplay.flushRefresh();
    LedStartupAnimation(pixels).play();
    batterySensor.begin();

    static RemoteDevelopmentService remoteDev;
    remoteDev.init(preferencesManager, *backDisplay);
    gRemoteDevelopmentService = &remoteDev;

    gBuzzer.setEnabled(preferencesManager.settings.enableBuzzer);
    // Any accepted press also skips the boot splash; it still does its normal job.
    remoteInputManager.setOnActionTaken([] {
        gBuzzer.trigger();
        einkDisplay.dismissSplash();
    });

    printLn("%s ready. FW version: %s, %s %s\n", Board::NAME, FW_VERSION, __DATE__, __TIME__);
    printLn("Read from config:");
    printLn("  enableWifi: %d", preferencesManager.settings.enableWifi);
    printLn("  enableBuzzer: %d", preferencesManager.settings.enableBuzzer);
    printLn("  brightness: %d", preferencesManager.settings.brightness);
    printLn("  wifiSSID: %s", preferencesManager.settings.wifiSSID);

    if (batterySensor.available()) {
        printLn("Battery: %u mV raw, %.3f V", static_cast<unsigned>(batterySensor.rawMilliVolts()), batterySensor.volts());
    }

    changeDeviceMode(DeviceModeState::ModeSwitchingMode);
}

/**
 * The one-shot low-battery message. Shown once per entry into the low state -
 * the monitor re-arms only above its exit threshold. #17 (3.3 V warn / 3.0 V
 * shutdown) will reuse the same Overlay with different content.
 */
void showLowBatteryOverlay() {
    char line[8];
    snprintf(line, sizeof(line), "%u%%", batteryMonitor.percent());

    const OverlayContent content = {
        "LOW BATTERY",
        line,
        {Glyph::b, Glyph::A, Glyph::t, Glyph::t},
        Colors::Red,
        LOW_BATTERY_OVERLAY_MS
    };

    overlay.show(content, millis());
    gBuzzer.playLowBattery();
}

void loop() {
    // First, before the frame gate: polls the panel's BUSY pin on every pass.
    einkDisplay.update();

    if (einkDisplay.available() && millis() - lastEInkStatsLog >= 30000) {
        lastEInkStatsLog = millis();
        printLn("EInk: worst start %lu us, worst finish %lu us, partials %lu (%lu since full), full %lu, timeouts %lu, busy never rose %lu",
                (unsigned long) einkDisplay.worstStartUs(), (unsigned long) einkDisplay.worstFinishUs(),
                (unsigned long) einkDisplay.refreshes(), (unsigned long) einkDisplay.partialsSinceFull(),
                (unsigned long) einkDisplay.fullRefreshes(), (unsigned long) einkDisplay.timeouts(),
                (unsigned long) einkDisplay.busyNeverRose());
    }

    gRemoteDevelopmentService->loop();
    remoteInputManager.handleInput(interruptTriggeredGpio);
    gBuzzer.loop();
    batterySensor.loop();
    batteryMonitor.loop(millis());

    if (batteryMonitor.isLow() != lastBatteryLow) {
        lastBatteryLow = batteryMonitor.isLow();
        // Not persisted: the config keeps whatever the user set, the cap just
        // limits what reaches the LEDs while the pack is low.
        ledDisplay.setBrightnessCap(lastBatteryLow ? LOW_BATTERY_BRIGHTNESS_CAP : 255);
        printLn("Battery %s (%u%%)", lastBatteryLow ? "LOW" : "recovered", batteryMonitor.percent());
    }

    if (batteryMonitor.takeLowWarning()) {
        showLowBatteryOverlay();
    }

    if (batterySensor.available() && millis() - lastBatteryLogMs >= 10000) {
        lastBatteryLogMs = millis();
        printLn("Battery: %u mV raw, %.3f V, %u%%", static_cast<unsigned>(batterySensor.rawMilliVolts()),
                batterySensor.volts(), batteryMonitor.percent());
    }

    // At most 20 fps, for now
    if (millis() - lastUpdate < 50) {
        return;
    }

    lastUpdate = millis();

    // While an overlay runs it owns all three displays and the active mode is
    // paused - no input, no rendering. Its state and timers are untouched, so a
    // pending score commit lands on the first frame after.
    if (overlay.active(lastUpdate)) {
        overlay.render(ledDisplay, *backDisplay, einkDisplay);
        return;
    }

    if (overlay.takeFinished()) {
        // Presses made while the message was up must not act on the view coming back.
        remoteInputManager.clearLatches();
        Overlay::resetLedState(ledDisplay);

        if (deviceMode) {
            deviceMode->restoreView();
        }
    }

    if (deviceMode) {
        deviceMode->loop();
    }
}
