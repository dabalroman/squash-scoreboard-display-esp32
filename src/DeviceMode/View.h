#ifndef VIEW_H
#define VIEW_H

#include "Display/BackDisplay.h"

class View {
protected:
    bool shouldRenderGlyphs = true;
    bool shouldRenderBack = true;

public:
    virtual ~View() = default;

    virtual void handleInput(RemoteInputManager &remoteInputManager) = 0;

    virtual void renderGlyphs(GlyphDisplay &glyphDisplay) = 0;

    virtual void renderScreen(BackDisplay &backDisplay) = 0;

    void queueRender() {
        shouldRenderGlyphs = true;
        shouldRenderBack = true;
    }
};

#endif //VIEW_H
