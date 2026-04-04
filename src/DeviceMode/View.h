#ifndef VIEW_H
#define VIEW_H

#include "Display/BackDisplay.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "RemoteInput/RemoteInputManager.h"

class View {
protected:
    bool shouldRenderLedDisplay = true;
    bool shouldRenderBack = true;

public:
    virtual ~View() = default;

    virtual void handleInput(RemoteInputManager &remoteInputManager) = 0;

    virtual void renderLedDisplay(LedDisplay &ledDisplay) = 0;

    virtual void renderScreen(BackDisplay &backDisplay) = 0;

    void queueRender() {
        shouldRenderLedDisplay = true;
        shouldRenderBack = true;
    }
};

#endif //VIEW_H
