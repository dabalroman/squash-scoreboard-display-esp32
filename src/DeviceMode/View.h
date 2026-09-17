#ifndef VIEW_H
#define VIEW_H

#include "Display/BackDisplay.h"
#include "Display/EInk/EInkDisplay.h"
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

    void queueRender() {
        shouldRenderLedDisplay = true;
        shouldRenderBack = true;
        shouldRenderEInk = true;
    }
};

#endif //VIEW_H
