#ifndef VIEW_H
#define VIEW_H

class View {
public:
    virtual ~View() = default;

    virtual void handleInput(RemoteInputManager &remoteInputManager) = 0;

    virtual void render(GlyphDisplay &glyphDisplay, Adafruit_SSD1306 &backDisplay) = 0;
};

#endif //VIEW_H
