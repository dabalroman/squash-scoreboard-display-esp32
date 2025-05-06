#ifndef VIEW_H
#define VIEW_H

class View {
protected:
    bool shouldRenderGlyphs = true;
    bool shouldRenderBack = true;

public:
    virtual ~View() = default;

    virtual void handleInput(RemoteInputManager &remoteInputManager) = 0;

    virtual void renderGlyphs(GlyphDisplay &glyphDisplay) = 0;

    virtual void renderBack(Adafruit_SSD1306 &backDisplay) = 0;

    void queueRender() {
        shouldRenderGlyphs = true;
        shouldRenderBack = true;
    }
};

#endif //VIEW_H
