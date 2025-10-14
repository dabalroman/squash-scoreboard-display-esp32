#ifndef MODE_SWITCHING_VIEW_H
#define MODE_SWITCHING_VIEW_H

#include "DeviceMode/DeviceModeState.h"
#include "DeviceMode/View.h"
#include "Display/GlyphDisplay.h"
#include "Display/Scrollable.h"
#include "Display/ScrollableWidget.h"

enum Options {
    Squash = 0,
    Volleyball = 1,
    ShortVolleyball = 2,
    Config = 3,
};

class ModeSwitchingView final : public View {
    std::function<void(DeviceModeState)> onDeviceModeChange;

    const std::vector<String> optionsList = {
        "  Squash  ",
        "Volleyball",
        "VolleyB 15",
        " [Config] ",
    };

    Scrollable scrollable;
    ScrollableWidget scrollableWidget;

public:
    explicit ModeSwitchingView(
        const std::function<void(DeviceModeState)> &onDeviceModeChange
    )
        : onDeviceModeChange(onDeviceModeChange), scrollable(optionsList), scrollableWidget(scrollable) {
    }

    static uint8_t clamp(const uint8_t value, const uint8_t min, const uint8_t max) {
        if (value < min) {
            return min;
        }

        if (value > max) {
            return max;
        }

        return value;
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (remoteInputManager.buttonA.takeActionIfPossible()) {
            scrollable.cycleSelectedOption(-1);
            queueRender();
        }

        if (remoteInputManager.buttonB.takeActionIfPossible()) {
            scrollable.cycleSelectedOption(1);
            queueRender();
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            switch (scrollable.getSelectedOptionId()) {
                case Options::Squash:
                    onDeviceModeChange(DeviceModeState::SquashMode);
                    break;
                case Options::Volleyball:
                    onDeviceModeChange(DeviceModeState::VolleyballMode);
                    break;
                case Options::ShortVolleyball:
                    onDeviceModeChange(DeviceModeState::ShortVolleyballMode);
                    break;
                case Options::Config:
                    onDeviceModeChange(DeviceModeState::ConfigMode);
                    break;
                default:
                    break;
            }

            queueRender();
        }
    }

    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        if (!shouldRenderGlyphs) {
            return;
        }

        Color color;

        switch (scrollable.getSelectedOptionId()) {
            default:
            case Options::Squash:
                color = Colors::Green;
                glyphDisplay.setGlyphsGlyph(Glyph::D5, Glyph::D0, Glyph::U, Glyph::A);
                break;
            case Options::Volleyball:
                color = Colors::Yellow;
                glyphDisplay.setGlyphsGlyph(Glyph::U, Glyph::A, Glyph::L, Glyph::L);
                break;
            case Options::ShortVolleyball:
                color = Colors::Yellow;
                glyphDisplay.setGlyphsGlyph(Glyph::U, Glyph::A, Glyph::D1, Glyph::D5);
                break;
            case Options::Config:
                color = Colors::White;
                glyphDisplay.setGlyphsGlyph(Glyph::C, Glyph::F, Glyph::G, Glyph::Empty);
                break;
        }

        glyphDisplay.setGlyphsColor(color, color);
        glyphDisplay.setIndicatorAppearancePlayerA(color);
        glyphDisplay.setIndicatorAppearancePlayerB(color);
        glyphDisplay.display();

        shouldRenderGlyphs = false;
    }

    void renderScreen(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        scrollableWidget.render(backDisplay);
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //MODE_SWITCHING_VIEW_H
