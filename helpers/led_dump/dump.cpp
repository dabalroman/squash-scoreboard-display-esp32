// Host-side golden dump of every LED pixel the display layer writes.
// Build against any include root (v1_snapshot, or ../../src with defines).
#include <cstdio>
#include <cstring>

#include "Display/LedDisplay/LedDisplay.h"

uint32_t g_fakeMillis = 0;
CFastLED FastLED;

static const int PIXELS = 112;

#ifdef LEDBAR_LAMBDA
#define SET_LEDBAR_STATE(d, s) (d).setLedBarState([&] { return (s); })
#else
#define SET_LEDBAR_STATE(d, s) (d).setLedBarState(s)
#endif

// ---------------------------------------------------------------- Dump A ---

static void fill(CRGB *buf, const CRGB &c) {
    for (int i = 0; i < PIXELS; i++) buf[i] = c;
}

static void dumpGlyphs() {
    static const char *phaseNames[] = {"off", "on-visible", "on-dark"};
    static const bool phaseBlink[] = {false, true, true};
    static const uint32_t phaseTick[] = {100, 300, 100};
    static const int glyphIds = 7;
    const CRGB sentinel1(1, 2, 3);
    const CRGB sentinel2(4, 5, 6);
    const Color glyphColor(0x12, 0x34, 0x56);

    for (int g = 0; g <= 36; g++) {
        for (int id = 0; id < glyphIds; id++) {
            for (int ph = 0; ph < 3; ph++) {
                CRGB pass1[PIXELS];
                CRGB pass2[PIXELS];
                CRGB *passes[] = {pass1, pass2};
                const CRGB *sentinels[] = {&sentinel1, &sentinel2};

                for (int p = 0; p < 2; p++) {
                    fill(passes[p], *sentinels[p]);
                    LedGlyph glyph(passes[p], static_cast<GlyphId>(id));
                    glyph.setGlyph(static_cast<Glyph>(g));
                    glyph.setColor(glyphColor);
                    glyph.setBlinking(phaseBlink[ph]);
                    glyph.render(phaseTick[ph]);
                }

                bool any = false;
                for (int i = 0; i < PIXELS; i++) {
                    const bool w1 = pass1[i] != sentinel1;
                    const bool w2 = pass2[i] != sentinel2;
                    if (!w1 && !w2) continue;
                    const CRGB &c = w1 ? pass1[i] : pass2[i];
                    printf("g=%d id=%d phase=%s idx=%d rgb=%u,%u,%u\n", g, id, phaseNames[ph], i, c.r, c.g, c.b);
                    any = true;
                }
                if (!any) {
                    printf("g=%d id=%d phase=%s none\n", g, id, phaseNames[ph]);
                }
            }
        }
    }
}

// ---------------------------------------------------------------- Dump B ---

static CRGB frame[PIXELS];
static int stepNo = 0;

// Mirrors LedDisplay::display(): clear the buffer, then render.
static void renderStep(LedDisplay &d, const char *label, uint32_t tick) {
    g_fakeMillis = tick;
    FastLED.clear();
    d.render();
    printf("step=%d %s tick=%u\n", stepNo++, label, tick);
    bool any = false;
    for (int i = 0; i < PIXELS; i++) {
        const CRGB &c = frame[i];
        if (c.r == 0 && c.g == 0 && c.b == 0) continue;
        printf("idx=%d rgb=%u,%u,%u\n", i, c.r, c.g, c.b);
        any = true;
    }
    if (!any) printf("all-black\n");
}

// Blink phases: tick % 500 < 250 is dark, >= 250 is visible.
static const uint32_t VIS = 1300;
static const uint32_t DARK = 1100;

static void dumpFrames() {
    FastLED.registerBuffer(frame, PIXELS);
    LedDisplay d(frame);

    renderStep(d, "initial", VIS);

    d.setNumericValue(7, 42);
    renderStep(d, "setNumericValue(7,42) default colours", VIS);

    d.setGlyphsColor(Colors::Red, Colors::Green, Colors::Blue, Colors::Yellow);
    renderStep(d, "setGlyphsColor(4)", VIS);

    d.setGlyphsColor(Colors::Pink, Colors::Aqua);
    d.setNumericValue(99, 3);
    renderStep(d, "setGlyphsColor(2) setNumericValue(99,3)", VIS);

    d.setGlyphsGlyph(Glyph::P, Glyph::A, Glyph::d, Glyph::E);
    renderStep(d, "setGlyphsGlyph(P,A,d,E)", VIS);

    d.setGlyphsAppearance(Colors::Orange, Colors::Violet, true, false);
    renderStep(d, "setGlyphsAppearance(Orange,Violet,blinkA) visible", VIS);
    renderStep(d, "setGlyphsAppearance(Orange,Violet,blinkA) dark", DARK);

    d.setGlyphBlinking(false, true, false, true);
    renderStep(d, "setGlyphBlinking(0,1,0,1) visible", VIS);
    renderStep(d, "setGlyphBlinking(0,1,0,1) dark", DARK);

    d.setGlyphBlinking(false, false);
    d.setColonAppearance(Colors::White);
    renderStep(d, "setGlyphBlinking(0,0) setColonAppearance(White)", DARK);

    d.setColonAppearance(Colors::Green, true);
    renderStep(d, "setColonAppearance(Green,blink) visible", VIS);
    renderStep(d, "setColonAppearance(Green,blink) dark", DARK);

    d.setColonAppearance();
    d.setPlayersIndicatorsState(true);
    renderStep(d, "setColonAppearance() setPlayersIndicatorsState(true) default colours", VIS);

    d.setSameSideMode(false);
    d.setIndicatorAppearancePlayerA(Colors::Red);
    d.setIndicatorAppearancePlayerB(Colors::Blue, true);
    renderStep(d, "sameSide=false indA(Red) indB(Blue,blink) visible", VIS);
    renderStep(d, "sameSide=false indA(Red) indB(Blue,blink) dark", DARK);

    d.setSameSideMode(true);
    d.setIndicatorAppearancePlayerA(Colors::Yellow, true);
    d.setIndicatorAppearancePlayerB(Colors::Aqua);
    renderStep(d, "sameSide=true indA(Yellow,blink) indB(Aqua) visible", VIS);
    renderStep(d, "sameSide=true indA(Yellow,blink) indB(Aqua) dark", DARK);

    d.setPlayersIndicatorsState(false);
    renderStep(d, "setPlayersIndicatorsState(false)", VIS);
    d.setSameSideMode(false);
    d.setPlayersIndicatorsState(true);

    std::array<LedBarPixel, LedBar::PIXEL_COUNT> state;
    for (int i = 0; i < LedBar::PIXEL_COUNT; i++) {
        state[i].color = CRGB((uint8_t)(10 * i + 1), (uint8_t)(255 - 7 * i), (uint8_t)(i * i));
        state[i].isBlinking = (i % 3) == 1;
    }
    state[5].color = CRGB::Black;
    state[20].color = CRGB::White;
    SET_LEDBAR_STATE(d, state);
    renderStep(d, "setLedBarState visible", VIS);
    renderStep(d, "setLedBarState dark", DARK);

    d.startCelebration(Colors::Green);
    renderStep(d, "startCelebration(Green) t0", 0);
    renderStep(d, "startCelebration(Green) t1", 777);
    renderStep(d, "startCelebration(Green) t2", 123457);

    d.resetHistoryBar();
    renderStep(d, "resetHistoryBar", VIS);

    SET_LEDBAR_STATE(d, state);
    d.setNumericValue(0, 0);
    d.setGlyphsAppearance(Colors::White, Colors::White);
    renderStep(d, "setLedBarState after reset, 00:00 White", DARK);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "glyphs") == 0) {
        dumpGlyphs();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "frames") == 0) {
        dumpFrames();
        return 0;
    }
    fprintf(stderr, "usage: %s glyphs|frames\n", argv[0]);
    return 2;
}
