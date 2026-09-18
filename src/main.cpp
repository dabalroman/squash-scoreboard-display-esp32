#include <version.h>

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FastLED.h>

#include "Strings.h"
#include "Board.h"

#include "DeviceMode/DeviceModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Animation/LedSweepAnimation.h"
#include "DeviceMode/DeviceMode.h"
#include "DeviceMode/ConfigMode/ConfigMode.h"
#include "DeviceMode/ModeSwitcherMode/ModeSwitchingMode.h"
#include "DeviceMode/SquashMode/SquashMode.h"
#include "DeviceMode/VolleyballMode/VolleyballMode.h"
#include "DeviceMode/PadelMode/PadelMode.h"
#include "DeviceMode/PlayerSetupMode/PlayerSetupMode.h"
#include "Tournament/Rules/ShortVolleyballRules.h"
#include "Tournament/Rules/VolleyballRules.h"
#include "RemoteInput/RemoteInputManager.h"
#include "Buzzer.h"
#include "BatterySensor.h"
#include "BatteryMonitor.h"
#include "Display/Overlay.h"
#include "Display/EInk/EInkDisplay.h"
#include "Display/EInk/PlayerSetupWebUi.h"
#include "PlayerRoster.h"
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
bool lowBatteryLatchActive = false;

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

// A mode change is requested from inside the outgoing mode's own loop(), so the
// swap itself is deferred to one safe point in loop() - see requestDeviceMode().
DeviceModeState pendingMode = DeviceModeState::ModeSwitchingMode;
bool modeChangePending = false;

const FactoryPlayer FACTORY_PLAYERS[] = {
    {"Adrian", PlayerColors::Green},
    {"Roman", PlayerColors::Yellow},
    {"Basia", PlayerColors::Magenta},
    {"Krystian", PlayerColors::Blue},
    {"Jola", PlayerColors::Red},
    {"Cegiel", PlayerColors::Pink}
};
constexpr uint8_t FACTORY_PLAYER_COUNT = sizeof(FACTORY_PLAYERS) / sizeof(FACTORY_PLAYERS[0]);

PlayerRoster playerRoster;
PlayerSetupWebUi playerSetupWebUi(playerRoster, preferencesManager);

void initHardware() {
    Wire.begin(Board::OLED_SDA, Board::OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_SSD1106_I2C_ADDRESS)) {
        printLn("SSD1106 allocation failed!");
    }

    backDisplay = std::make_unique<BackDisplay>(&display);

    FastLED.addLeds<NEOPIXEL, Board::LED_DATA>(pixels, Board::LED_COUNT);
    ledDisplay.setBrightness(preferencesManager.settings.brightness);
    FastLED.setMaxRefreshRate(400);
    FastLED.clear();
    FastLED.show();

    pinMode(Board::RF_D0, INPUT);
    pinMode(Board::RF_D1, INPUT);
    pinMode(Board::RF_D2, INPUT);
    pinMode(Board::RF_D3, INPUT);

    attachInterrupt(digitalPinToInterrupt(Board::RF_D0), onRemoteReceiverInterrupt_d0, RISING);
    attachInterrupt(digitalPinToInterrupt(Board::RF_D1), onRemoteReceiverInterrupt_d1, RISING);
    attachInterrupt(digitalPinToInterrupt(Board::RF_D2), onRemoteReceiverInterrupt_d2, RISING);
    attachInterrupt(digitalPinToInterrupt(Board::RF_D3), onRemoteReceiverInterrupt_d3, RISING);
}

/**
 * What every mode's onDeviceModeChange callback calls. It only records the wish:
 * the call arrives from handleInput(), deep inside the outgoing mode's own loop(),
 * and building the new mode here would free `this` and the active view under the
 * three render calls that still follow. loop() applies it on the next frame.
 */
void requestDeviceMode(const DeviceModeState deviceModeState) {
    pendingMode = deviceModeState;
    modeChangePending = true;
}

void buildDeviceMode(const DeviceModeState deviceModeState) {
    deviceState = deviceModeState;

    switch (deviceModeState) {
        default:
        case DeviceModeState::ModeSwitchingMode:
            deviceMode = std::make_unique<ModeSwitchingMode>(
                ledDisplay,
                *backDisplay,
                einkDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { requestDeviceMode(state); },
                batteryMonitor
            );
            break;

        case DeviceModeState::ConfigMode:
            deviceMode = std::make_unique<ConfigMode>(
                ledDisplay,
                *backDisplay,
                einkDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { requestDeviceMode(state); },
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
                [](const DeviceModeState state) { requestDeviceMode(state); },
                playerRoster.profiles(),
                []{ gBuzzer.playCelebration(); }
            );
            break;

        case DeviceModeState::VolleyballMode:
            deviceMode = std::make_unique<VolleyballMode>(
                ledDisplay,
                *backDisplay,
                einkDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { requestDeviceMode(state); },
                playerRoster.profiles(),
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
                [](const DeviceModeState state) { requestDeviceMode(state); },
                playerRoster.profiles(),
                std::make_unique<ShortVolleyballRules>(),
                []{ gBuzzer.playCelebration(); }
            );
            break;

        case DeviceModeState::PlayerSetupMode:
            deviceMode = std::make_unique<PlayerSetupMode>(
                ledDisplay,
                *backDisplay,
                einkDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { requestDeviceMode(state); },
                *gRemoteDevelopmentService,
                playerSetupWebUi
            );
            break;

        case DeviceModeState::PadelMode:
            deviceMode = std::make_unique<PadelMode>(
                ledDisplay,
                *backDisplay,
                einkDisplay,
                remoteInputManager,
                [](const DeviceModeState state) { requestDeviceMode(state); },
                playerRoster.profiles(),
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
    // Before any mode is built: every mode is handed playerRoster.profiles().
    playerRoster.load(FACTORY_PLAYERS, FACTORY_PLAYER_COUNT);
    initHardware();
    einkDisplay.begin();   // V2: blocks ~3 s once (initial full refresh), then the splash
    // begin() only queues the splash; the sweep below blocks before loop() can send it.
    einkDisplay.flushRefresh();
    LedSweepAnimation(pixels, LedSweepAnimation::bootParams()).play();
    batterySensor.begin();

    static RemoteDevelopmentService remoteDev;
    // Before init(): the routes are registered with the port-80 server whenever it
    // is created, which may be here or later, when the roster editor raises its AP.
    remoteDev.setExtraRouteRegistrar([](WebServer &server) { playerSetupWebUi.registerRoutes(server); });
    // Same capture rule as above - file-scope globals only. It is here, and not in
    // RemoteDevelopmentService, so that class never learns about LedDisplay or
    // EInkDisplay: it reports a stage, main.cpp decides what the board shows.
    remoteDev.setUpdateStatusHandler([](const FirmwareUpdateStage stage, const char *detail) {
        // An upload is someone using the roster editor. Without this the 15-minute
        // idle close can drop the AP out from under a phone that is mid-update.
        playerSetupWebUi.noteActivity();

        // Nothing to paint on success: the board reboots a moment later and the
        // splash puts the panel back on its own.
        if (stage == FirmwareUpdateStage::Succeeded) {
            return;
        }

        const bool failed = stage == FirmwareUpdateStage::Failed;
        const Color color = failed ? Colors::Red : Colors::White;

        ledDisplay.resetAnimations();
        ledDisplay.setColonAppearance();
        ledDisplay.setGlyphsText(failed ? Str::LED_OTA_FAILED : Str::LED_OTA_PROGRESS);
        ledDisplay.setGlyphsColor(color, color);
        ledDisplay.setGlyphBlinking(false, false);
        ledDisplay.setPlayersIndicatorsState(true);
        ledDisplay.setIndicatorAppearancePlayerA(color, false);
        ledDisplay.setIndicatorAppearancePlayerB(color, false);
        ledDisplay.setBorderEnabled(true);
        ledDisplay.setBorderAppearance(color, color, false, false);
        ledDisplay.display();

        backDisplay->clear();
        backDisplay->initSmallFont();
        backDisplay->setCursorToLine();
        backDisplay->println(failed ? Str::OTA_OLED_FAILED : Str::OTA_OLED_STARTED);
        if (detail != nullptr) {
            backDisplay->setCursorToLine(0, 1);
            backDisplay->println(detail);
        }
        backDisplay->display();

        // WebServer reads the whole multipart body inside handleClient(), so loop()
        // - and with it the e-paper's refresh pump - is stalled for the entire
        // upload. This screen has to be driven to completion right here, and as a
        // full refresh: an update screen must not carry the ghost of the match
        // behind it.
        einkDisplay.dismissSplash();
        einkDisplay.showMessage(
            failed ? (detail != nullptr ? detail : Str::OTA_EINK_TITLE_ERROR) : Str::OTA_EINK_TITLE_STARTED,
            failed ? Str::OTA_EINK_LINE_FAILED : Str::OTA_EINK_LINE_STARTED,
            true);
        einkDisplay.flushRefresh();
    });
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
    printLn("  enableDevMode: %d", preferencesManager.settings.enableDevMode);
    printLn("  enableBuzzer: %d", preferencesManager.settings.enableBuzzer);
    printLn("  brightness: %d", preferencesManager.settings.brightness);
    printLn("  wifiSSID: %s", preferencesManager.settings.wifiSSID);
    printLn("Roster: %u players", static_cast<unsigned>(playerRoster.size()));

    if (batterySensor.available()) {
        printLn("Battery: %u mV raw, %.3f V", static_cast<unsigned>(batterySensor.rawMilliVolts()), batterySensor.volts());
    }

    buildDeviceMode(DeviceModeState::ModeSwitchingMode);
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
        Str::OVERLAY_LOW_BATTERY_TITLE,
        line,
        LedText::toWord(Str::LED_OVERLAY_LOW_BATTERY),
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
    // Right after it: a save handled above arms a restart a few hundred ms out, so
    // the socket flushes before the board goes down.
    playerSetupWebUi.loop();
    remoteInputManager.handleInput(interruptTriggeredGpio);
    gBuzzer.loop();
    batterySensor.loop();
    batteryMonitor.loop(millis());

    if (!lowBatteryLatchActive && batteryMonitor.isLow()) {
        lowBatteryLatchActive = true;
        ledDisplay.setLowPowerMode(true);
        printLn("Battery LOW (%u%%)", batteryMonitor.percent());
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

    // Applied here, not in the callback: the request arrives from inside the outgoing
    // mode's loop(), which would leave its own render calls running on a freed object.
    // After the overlay gate, because an overlay pauses the mode and owns all three
    // displays - the incoming constructor must not draw underneath it - and so the
    // restoreView() below lands on the new mode.
    if (modeChangePending) {
        // Cleared first, so a constructor that requests a change is honoured next frame.
        modeChangePending = false;
        // reset() before the build: the outgoing teardown (PlayerSetupMode drops its AP)
        // runs before the incoming constructor, and peak heap stays lower.
        deviceMode.reset();
        buildDeviceMode(pendingMode);
    }

    if (overlay.takeFinished()) {
        // Presses made while the message was up must not act on the view coming back.
        remoteInputManager.clearLatches();
        Overlay::resetLedState(ledDisplay);

        if (deviceMode) {
            deviceMode->restoreView();
        }
    }

    // Handle long press C
    if (remoteInputManager.buttonC.takeLongPressIfPossible()) {
        const bool handled = deviceMode && deviceMode->goBack();

        if (handled) {
            gBuzzer.playBack();
            remoteInputManager.preventTriggerForMs();
        }
    }

    if (deviceMode) {
        deviceMode->loop();
    }
}
