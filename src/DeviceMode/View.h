#ifndef VIEW_H
#define VIEW_H

#include "Display/BackDisplay.h"
#include "Display/EInk/EInkDisplay.h"
#include "Garmin/WatchCommand.h"
#include "Garmin/WatchState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "RemoteInput/RemoteInputManager.h"

class View {
protected:
    bool shouldRenderLedDisplay = true;
    bool shouldRenderBack = true;
    bool shouldRenderEInk = true;

public:
    virtual ~View() = default;

    virtual void handleInput(RemoteInputManager &remoteInputManager) = 0;

    virtual void initLedDisplay(LedDisplay &ledDisplay) {}

    virtual void renderLedDisplay(LedDisplay &ledDisplay) = 0;

    virtual void initBackDisplay(BackDisplay &backDisplay) {}

    virtual void renderBackDisplay(BackDisplay &backDisplay) = 0;

    virtual void initEInkDisplay(EInkDisplay &einkDisplay) {}

    /**
     * Called every frame, like the other two. Unlike the LED display, the e-paper
     * must only refresh on real change: EInkDisplay compares the values it is
     * given with what it shows, so views pass values and never rely on
     * queueRender() (the game-playing views never call it). Default: blank.
     */
    virtual void renderEInkDisplay(EInkDisplay &einkDisplay) {
        einkDisplay.showBlank();
    }

    /**
     * A watch command (Garmin App Remote), applied in loop() like a fob press. A view that
     * takes commands calls the same action methods as its button handlers. Default: this
     * screen takes none.
     */
    virtual Garmin::AckStatus handleWatchCommand(const WatchCommand &) {
        return Garmin::AckStatus::WrongScreen;
    }

    // The screen the watches see. Default: board busy.
    virtual void describeForWatch(WatchState &state) const {
        state.setBusy(Garmin::ScreenId::Booting);
    }

    void queueRender() {
        shouldRenderLedDisplay = true;
        shouldRenderBack = true;
        shouldRenderEInk = true;
    }

    // The OLED only: for chrome drawn outside the view (the watch badge), with no LED effect.
    void queueBackRender() {
        shouldRenderBack = true;
    }
};

#endif //VIEW_H
