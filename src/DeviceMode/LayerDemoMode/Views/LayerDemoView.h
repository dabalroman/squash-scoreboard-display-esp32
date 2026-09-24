#ifndef LAYER_DEMO_VIEW_H
#define LAYER_DEMO_VIEW_H

#include "PlayerPalette.h"
#include "Strings.h"
#include "DeviceMode/View.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "RemoteDevelopmentService/LoggerHelper.h"

/**
 * Temporary (#52). SquashGameOverView's LED setup with fake data (11:07, left
 * won), the celebration replaying on its own. A/B cycle the sweep's blend, short
 * C toggles breathing on layer 1, D replays now; long C goes back (the mode).
 */
class LayerDemoView final : public View {
    // 3 cycles run 3000 ms; one more second of the plain screen between replays.
    enum : uint32_t { REPLAY_EVERY_MS = 4000 };
    enum : uint8_t { BLEND_COUNT = 4 };

    uint8_t blendIndex = 1;   // Screen, LedDisplay's default
    bool breathing = false;
    bool restartPending = true;
    bool breathingDirty = false;
    uint32_t startedMs = 0;

    static BlendMode blendAt(const uint8_t index) {
        switch (index) {
            case 0: return BlendMode::Normal;
            case 1: return BlendMode::Screen;
            case 2: return BlendMode::Add;
            default: return BlendMode::Lighten;
        }
    }

    void cycleBlend(const int8_t step) {
        blendIndex = static_cast<uint8_t>((blendIndex + BLEND_COUNT + step) % BLEND_COUNT);
        restartPending = true;
        shouldRenderBack = true;
        printLn("Layer demo: blend %s", blendName(blendAt(blendIndex)));
    }

public:
    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonA.takeActionIfPossible()) {
            cycleBlend(-1);
        }

        if (remoteInputManager.buttonB.takeActionIfPossible()) {
            cycleBlend(1);
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            breathing = !breathing;
            breathingDirty = true;
            shouldRenderBack = true;
            printLn("Layer demo: breathing %s", breathing ? "on" : "off");
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            restartPending = true;
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        const Color left = PlayerColors::Red;
        const Color right = PlayerColors::Blue;

        ledDisplay.resetAnimations();
        ledDisplay.setColonAppearance();
        ledDisplay.setNumericValue(11, 7);
        ledDisplay.setGlyphsAppearance(left, right);
        ledDisplay.setPlayersIndicatorsState(true);
        ledDisplay.setIndicatorAppearancePlayerA(left, true);
        ledDisplay.setIndicatorAppearancePlayerB(right, false);
        ledDisplay.setBorderEnabled(true);
        ledDisplay.setBorderAppearance(left, right, true, false);
        // A fake result bar, so the sweep is judged over the bar too (V1).
        ledDisplay.setLedBarState([&] {
            std::array<LedBarPixel, LedBar::PIXEL_COUNT> bar;
            for (uint8_t i = 0; i < 11; i++) bar[i].color = CRGB(left.r, left.g, left.b);
            for (uint8_t i = 0; i < 7; i++) bar[LedBar::PIXEL_COUNT - 1 - i].color = CRGB(right.r, right.g, right.b);
            return bar;
        });

        // resetAnimations() above detached both layers.
        restartPending = true;
        breathingDirty = breathing;
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        const uint32_t now = millis();

        if (restartPending || now - startedMs >= REPLAY_EVERY_MS) {
            ledDisplay.setCelebrationBlend(blendAt(blendIndex));
            ledDisplay.startCelebration(PlayerColors::Red, true);
            startedMs = now;
            restartPending = false;
        }

        if (breathingDirty) {
            ledDisplay.setBreathing(breathing);
            breathingDirty = false;
        }

        ledDisplay.display();
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.initSmallFont();
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available()) {
            return;
        }

        einkDisplay.showBlank();
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        backDisplay.setCursorToLine(0, 0);
        backDisplay.print(Str::MODE_OPTION_LAYER_DEMO);
        backDisplay.setCursorToLine(0, 1);
        backDisplay.print(blendName(blendAt(blendIndex)));
        backDisplay.setCursorToLine(0, 2);
        backDisplay.print(breathing ? Str::LAYER_DEMO_BREATH_ON : Str::LAYER_DEMO_BREATH_OFF);
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //LAYER_DEMO_VIEW_H
