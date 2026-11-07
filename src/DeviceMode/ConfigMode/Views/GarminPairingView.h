#ifndef GARMIN_PAIRING_VIEW_H
#define GARMIN_PAIRING_VIEW_H

#include "Strings.h"
#include "DeviceMode/View.h"
#include "DeviceMode/ConfigMode/ConfigModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Garmin/GarminService.h"

/**
 * The pairing window: opened on entry, closed by the destructor, so every way out (C, D,
 * long C, the 120 s expiry, leaving the mode) closes it the same way. Shows the 4-digit
 * code on all three displays and the time left on the OLED and the e-paper.
 *
 * The outcome is a ring on the LEDs while the OLED and e-paper keep the code: green on
 * success, then out of CONFIG to the sport menu; red on expiry, then back to GARMIN.
 * C/D leave at once, with no ring.
 */
class GarminPairingView final : public View {
    // The e-paper countdown step. Every change is a partial refresh (~0.5 s of flicker):
    // a 1 s step would be 120 partials per window, nearly EInkPolicy::MAX_PARTIALS, and
    // force a full flash mid-pairing. 30 s is 4; the OLED carries the exact seconds.
    enum : uint32_t { EINK_STEP_S = 30 };
    // The LED code blinks over its last seconds.
    enum : uint32_t { BLINK_FROM_S = 10 };
    enum : uint8_t { CODE_BASELINE = 50, COUNTDOWN_BASELINE = 63 };

    // Paired / Expired: the outcome ring is pending, running or just ended (ringDone).
    enum class Phase : uint8_t { Code, Paired, Expired, Leaving };

    GarminService &garminService;
    std::function<void(ConfigModeState)> onStateChange;
    std::function<void()> onPaired;
    std::function<void()> onLeaveConfig;

    uint16_t code = 0;
    uint32_t pairingsAtOpen = 0;
    uint32_t shownSeconds = UINT32_MAX;
    Phase phase = Phase::Code;
    bool ringStarted = false;
    bool ringDone = false;

    uint32_t remainingSeconds() const {
        return (garminService.pairingRemainingMs() + 999) / 1000;
    }

    void leave() {
        phase = Phase::Leaving;
        onStateChange(ConfigModeState::Garmin);
    }

    bool showingRing() const {
        return phase == Phase::Paired || phase == Phase::Expired;
    }

public:
    // onPaired fires as the green ring starts; onLeaveConfig once it ends.
    GarminPairingView(
        GarminService &garminService,
        const std::function<void(ConfigModeState)> &onStateChange,
        const std::function<void()> &onPaired,
        const std::function<void()> &onLeaveConfig
    )
        : garminService(garminService), onStateChange(onStateChange),
          onPaired(onPaired), onLeaveConfig(onLeaveConfig) {
        code = garminService.openPairing();
        pairingsAtOpen = garminService.completedPairings();
    }

    // Success, expiry or a cancel: the view no longer takes a back.
    bool outcomeShown() const {
        return phase != Phase::Code;
    }

    ~GarminPairingView() override {
        garminService.closePairing();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (phase == Phase::Leaving) {
            return;
        }

        // Presses during the ring are dropped, not latched for the next view.
        if (showingRing()) {
            remoteInputManager.clearLatches();
            if (!ringDone) {
                return;
            }
            if (phase == Phase::Paired) {
                phase = Phase::Leaving;
                onLeaveConfig();
            } else {
                leave();
            }
            return;
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()
            || remoteInputManager.buttonD.takeActionIfPossible()) {
            leave();
            return;
        }

        // Done once a watch has paired and authenticated with its key - not at key delivery:
        // closing the window wipes the key reply the watch may not have read yet.
        if (garminService.completedPairings() != pairingsAtOpen) {
            garminService.closePairing();
            phase = Phase::Paired;
            onPaired();
            return;
        }

        // Expiry, or the window closed under us (the feature failed or stopped).
        if (!garminService.pairingOpen()) {
            phase = Phase::Expired;
            return;
        }

        const uint32_t seconds = remainingSeconds();
        if (seconds != shownSeconds) {
            shownSeconds = seconds;
            shouldRenderBack = true;
            shouldRenderLedDisplay = true;
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.resetAnimations();
        // restoreView() after an Overlay: the outcome is not replayed, the view hands over.
        if (phase != Phase::Code) {
            ringStarted = ringDone = true;
            return;
        }
        ledDisplay.setColonAppearance();
        ledDisplay.setGlyphsGlyph(
            LedDisplay::digitToGlyph(code / 1000 % 10),
            LedDisplay::digitToGlyph(code / 100 % 10),
            LedDisplay::digitToGlyph(code / 10 % 10),
            LedDisplay::digitToGlyph(code % 10)
        );
        ledDisplay.setGlyphsColor(Colors::Blue, Colors::Blue);
        ledDisplay.setPlayersIndicatorsState(true);
        ledDisplay.setIndicatorAppearancePlayerA(Colors::Blue);
        ledDisplay.setIndicatorAppearancePlayerB(Colors::Blue);
        ledDisplay.setBorderEnabled(true);
        ledDisplay.setBorderAppearance(Colors::Blue, Colors::Blue);
    }

    // Every tick: the glyph blink and the ring are per-frame renders.
    void renderLedDisplay(LedDisplay &ledDisplay) override {
        if (phase == Phase::Leaving) {
            return;
        }

        if (showingRing()) {
            if (!ringStarted) {
                ringStarted = true;
                ledDisplay.startRing(phase == Phase::Paired ? Colors::Green : Colors::Red);
            }
            // Checked before display(): an ended ring would show one black frame.
            if (!ledDisplay.ringActive()) {
                ringDone = true;
                return;
            }
            ledDisplay.display();
            return;
        }

        const bool blink = shownSeconds <= BLINK_FROM_S;
        ledDisplay.setGlyphBlinking(blink, blink);
        ledDisplay.display();
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (!shouldRenderBack || phase != Phase::Code) {
            return;
        }

        char codeText[6];
        snprintf(codeText, sizeof(codeText), "%04u", static_cast<unsigned>(code));
        char countdown[20];
        snprintf(countdown, sizeof(countdown), Str::GARMIN_PAIR_COUNTDOWN_OLED_FMT,
                 static_cast<unsigned>(shownSeconds == UINT32_MAX ? remainingSeconds() : shownSeconds));

        backDisplay.clear();
        backDisplay.initBigFont();
        // Baselines 50 / 63 keep the code below the watch badge on both boards.
        backDisplay.setCursorFromTopLeft((128 - 4 * BackDisplay::ONE_CHAR_WIDTH_24pt7b) / 2, CODE_BASELINE);
        backDisplay.print(codeText);
        backDisplay.initSmallFont();
        backDisplay.setCursorFromTopLeft(0, COUNTDOWN_BASELINE);
        backDisplay.print(countdown);
        backDisplay.display();

        shouldRenderBack = false;
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        // The code screen stays through the ring: no extra partial for the outcome.
        if (!einkDisplay.available() || phase != Phase::Code) {
            return;
        }

        char codeText[6];
        snprintf(codeText, sizeof(codeText), "%04u", static_cast<unsigned>(code));

        // Rounded up to the step: the panel never claims less time than is left.
        const uint32_t seconds = shownSeconds == UINT32_MAX ? remainingSeconds() : shownSeconds;
        const uint32_t stepped = (seconds + EINK_STEP_S - 1) / EINK_STEP_S * EINK_STEP_S;
        char countdown[12];
        snprintf(countdown, sizeof(countdown), Str::GARMIN_PAIR_COUNTDOWN_FMT, static_cast<unsigned>(stepped));

        char paired[20];
        snprintf(paired, sizeof(paired), Str::GARMIN_PAIRED_FMT,
                 static_cast<unsigned>(garminService.pairedCount()));

        einkDisplay.showCode(Str::GARMIN_PAIR_TITLE, codeText, countdown, paired);
    }
};

#endif //GARMIN_PAIRING_VIEW_H
