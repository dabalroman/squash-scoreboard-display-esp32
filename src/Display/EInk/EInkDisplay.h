#ifndef EINK_DISPLAY_H
#define EINK_DISPLAY_H

/**
 * Hardware wrapper for the V2 e-paper (WeAct 2.9", 128x296). The only e-ink file
 * with `#if BOARD_REV`: the real implementation on V2, an empty stub with the
 * identical public API on V1, so call sites never test the board.
 *
 * Shares nothing with BackDisplay (the OLED) - no base class, helpers or
 * interface, and neither calls the other. See "V2 Guidelines.md", Part 2.
 *
 * Usage:
 *   begin()   once in setup(), blocking ~3 s (initial full refresh + splash).
 *   update()  at the very top of every loop() pass - polls BUSY, never blocks
 *             for the waveform; only the two ~10 ms SPI bursts per refresh.
 *
 * Change detection (e-ink is always guarded, unlike the LED display): a render
 * computes a cheap hash of the values it shows and asks commit(hash). Only on
 * true does it redraw gfx() and call requestRefresh(). Calling that 20x a second
 * is fine; unchanged content costs one compare, and changes arriving during a
 * refresh coalesce inside EInkAsync.
 *
 *   uint32_t h = EInkDisplay::HASH_SEED;
 *   h = EInkDisplay::hashAdd(h, scoreA);
 *   h = EInkDisplay::hashAdd(h, scoreB);
 *   if (eink.commit(h)) { draw into eink.gfx(); eink.requestRefresh(); }
 *
 * The hash must cover everything that screen draws - including which screen it
 * is (mix in a per-screen constant) - or two screens with equal values would
 * suppress each other's redraw.
 */

#include <Arduino.h>
#include <Adafruit_GFX.h>

#include "../../Board.h"

#if BOARD_REV == 2

#include <version.h>
#include "EInkAsync.h"

class EInkDisplay {
public:
    enum : uint16_t { PAPER = EInkAsync::PAPER, INK = EInkAsync::INK };
    enum : uint32_t { HASH_SEED = 2166136261u };   // FNV-1a offset basis

    EInkDisplay() : eink(Board::EINK_CS, Board::EINK_DC, Board::EINK_RST, Board::EINK_BUSY) {}

    bool available() const { return true; }

    // Blocking (~3 s, boot only): initial full refresh, then the splash is
    // requested as a normal async partial.
    void begin() {
        // GxEPD2 writes CS/DC/RST before pinMode on them; core 3.x logs
        // "IO n is not set as GPIO". Claiming them first is harmless on 2.0.17.
        const uint8_t pins[] = {Board::EINK_CS, Board::EINK_DC, Board::EINK_RST};
        for (const uint8_t pin : pins) {
            pinMode(pin, OUTPUT);
            digitalWrite(pin, HIGH);
        }

        eink.begin(Board::EINK_SCK, Board::EINK_MOSI, Board::EINK_CS);

        // The panel is mounted upside down (2026-09-17). Rotation 2 keeps
        // width() 128 x height() 296.
        eink.gfx().setRotation(2);

        drawSplash();
    }

    // Call on every loop() pass, before any frame gate.
    void update() { eink.update(); }

    Adafruit_GFX &gfx() { return eink.gfx(); }

    // Queue the current canvas for the panel. Coalesces while a refresh runs.
    void requestRefresh() { eink.requestRefresh(); }

    void showBlank() {
        if (content == Content::Blank) {
            return;
        }
        content = Content::Blank;
        eink.gfx().fillScreen(PAPER);
        eink.requestRefresh();
    }

    // True if `hash` differs from what is on the panel (or the panel shows
    // something else, like the splash); the caller must then redraw and request a
    // refresh. False means unchanged - skip drawing entirely.
    bool commit(const uint32_t hash) {
        if (content == Content::Hashed && hash == lastContentHash) {
            return false;
        }
        content = Content::Hashed;
        lastContentHash = hash;
        return true;
    }

    // Forget what is shown, so the next commit()/showBlank() redraws.
    void invalidate() { content = Content::None; }

    // FNV-1a over the 4 bytes of `value`. Cheap, order-sensitive.
    static uint32_t hashAdd(uint32_t hash, const uint32_t value) {
        for (uint8_t i = 0; i < 4; i++) {
            hash ^= (value >> (i * 8)) & 0xFFu;
            hash *= 16777619u;
        }
        return hash;
    }

    uint32_t worstStartUs() const { return eink.getStats().worstStartUs; }
    uint32_t worstFinishUs() const { return eink.getStats().worstFinishUs; }
    uint32_t refreshes() const { return eink.getStats().refreshes; }
    uint32_t timeouts() const { return eink.getStats().timeouts; }
    uint32_t busyNeverRose() const { return eink.getStats().busyNeverRose; }

private:
    enum class Content : uint8_t { None, Splash, Blank, Hashed };

    void drawSplash() {
        GFXcanvas1 &g = eink.gfx();
        const int16_t w = g.width();   // 128

        g.fillScreen(PAPER);
        g.setTextColor(INK);
        g.setTextWrap(false);

        // Orientation marker: arrow pointing at the top edge.
        g.fillTriangle(w / 2, 4, w / 2 - 12, 20, w / 2 + 12, 20, INK);
        g.fillRect(w / 2 - 4, 20, 8, 12, INK);
        printCentered(g, "TOP", 38, 1);

        printCentered(g, "SQUASH", 110, 3);   // 18 px/char -> 108 px wide
        printCentered(g, "SCORE", 145, 3);

        printCentered(g, "FW", 230, 1);
        printCentered(g, FW_VERSION, 244, 2);

        content = Content::Splash;
        eink.requestRefresh();
    }

    static void printCentered(GFXcanvas1 &g, const char *text, const int16_t y, const uint8_t size) {
        g.setTextSize(size);
        int16_t x1, y1;
        uint16_t tw, th;
        g.getTextBounds(text, 0, y, &x1, &y1, &tw, &th);
        g.setCursor((g.width() - (int16_t) tw) / 2, y);
        g.print(text);
    }

    EInkAsync eink;
    Content content = Content::None;
    uint32_t lastContentHash = 0;
};

#else

// V1 has no e-paper. Same API, all empty; nothing is allocated. commit() is
// always false, so callers never draw into gfx() here.
class EInkDisplay {
public:
    enum : uint16_t { PAPER = 1, INK = 0 };
    enum : uint32_t { HASH_SEED = 2166136261u };

    bool available() const { return false; }
    void begin() {}
    void update() {}

    // Only reachable if a caller ignores commit(). The canvas is a no-op sink;
    // it is not instantiated unless gfx() is actually called.
    Adafruit_GFX &gfx() {
        static NullGfx sink;
        return sink;
    }

    void requestRefresh() {}
    void showBlank() {}
    bool commit(const uint32_t) { return false; }
    void invalidate() {}
    static uint32_t hashAdd(const uint32_t hash, const uint32_t) { return hash; }

    uint32_t worstStartUs() const { return 0; }
    uint32_t worstFinishUs() const { return 0; }
    uint32_t refreshes() const { return 0; }
    uint32_t timeouts() const { return 0; }
    uint32_t busyNeverRose() const { return 0; }

private:
    class NullGfx : public Adafruit_GFX {
    public:
        NullGfx() : Adafruit_GFX(128, 296) {}
        void drawPixel(int16_t, int16_t, uint16_t) override {}
    };
};

#endif

#endif //EINK_DISPLAY_H
