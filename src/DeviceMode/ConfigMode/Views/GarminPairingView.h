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
 */
class GarminPairingView final : public View {
    // The e-paper countdown step. Every change is a partial refresh (~0.5 s of flicker):
    // a 1 s step would be 120 partials per window, nearly EInkPolicy::MAX_PARTIALS, and
    // force a full flash mid-pairing. 30 s is 4; the OLED carries the exact seconds.
    enum : uint32_t { EINK_STEP_S = 30 };
    // The LED code blinks over its last seconds.
    enum : uint32_t { BLINK_FROM_S = 10 };
    enum : uint8_t { CODE_BASELINE = 50, COUNTDOWN_BASELINE = 63 };

    GarminService &garminService;
    std::function<void(ConfigModeState)> onStateChange;

    uint16_t code = 0;
    uint32_t pairingsAtOpen = 0;
    uint32_t shownSeconds = UINT32_MAX;
    bool leaving = false;

    uint32_t remainingSeconds() const {
        return (garminService.pairingRemainingMs() + 999) / 1000;
    }

    void leave() {
        leaving = true;
        onStateChange(ConfigModeState::Garmin);
    }

public:
    GarminPairingView(GarminService &garminService, const std::function<void(ConfigModeState)> &onStateChange)
        : garminService(garminService), onStateChange(onStateChange) {
        code = garminService.openPairing();
        pairingsAtOpen = garminService.completedPairings();
    }

    ~GarminPairingView() override {
        garminService.closePairing();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (leaving) {
            return;
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()
            || remoteInputManager.buttonD.takeActionIfPossible()) {
            leave();
            return;
        }

        // Done once a watch has paired and authenticated with its key - not at key delivery:
        // closing the window wipes the key reply the watch may not have read yet. Also on
        // expiry, or when the window was closed under us (the feature failed or stopped).
        if (garminService.completedPairings() != pairingsAtOpen || !garminService.pairingOpen()) {
            leave();
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

    // Every tick: the glyph blink is a per-frame render.
    void renderLedDisplay(LedDisplay &ledDisplay) override {
        const bool blink = shownSeconds <= BLINK_FROM_S;
        ledDisplay.setGlyphBlinking(blink, blink);
        ledDisplay.display();
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (!shouldRenderBack || leaving) {
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
        if (!einkDisplay.available() || leaving) {
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
