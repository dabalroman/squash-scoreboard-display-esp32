// V1 golden dumps of every LED pixel the display layer writes: each glyph on each
// position in each blink phase, and a 22-step frame script over the LedDisplay API.
// Byte-identical to the goldens taken before the V2 work.
#include <string>

#include <unity.h>

#include "Display/LedDisplay/LedDisplay.h"
#include "../common/golden.h"
#include "../common/host_globals.h"

static const int PIXELS = 112;

void setUp() {}
void tearDown() {}

static std::string dumpGlyphs() {
    static const char *phaseNames[] = {"off", "on-visible", "on-dark"};
    static const bool phaseBlink[] = {false, true, true};
    static const uint32_t phaseTick[] = {100, 300, 100};
    static const int glyphIds = 7;
    const CRGB sentinel1(1, 2, 3);
    const CRGB sentinel2(4, 5, 6);
    const Color glyphColor(0x12, 0x34, 0x56);
    std::string out;

    for (int g = 0; g < GLYPH_COUNT; g++) {
        for (int id = 0; id < glyphIds; id++) {
            for (int ph = 0; ph < 3; ph++) {
                // Two sentinels, so a write of the sentinel colour itself is still seen.
                CRGB pass1[PIXELS];
                CRGB pass2[PIXELS];
                CRGB *passes[] = {pass1, pass2};
                const CRGB *sentinels[] = {&sentinel1, &sentinel2};

                for (int p = 0; p < 2; p++) {
                    fillPixels(passes[p], PIXELS, *sentinels[p]);
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
                    out += strf("g=%d id=%d phase=%s idx=%d rgb=%u,%u,%u\n", g, id, phaseNames[ph], i, c.r, c.g, c.b);
                    any = true;
                }
                if (!any) out += strf("g=%d id=%d phase=%s none\n", g, id, phaseNames[ph]);
            }
        }
    }
    return out;
}

class FrameScript {
    CRGB frame[PIXELS];
    int stepNo = 0;
    // Registered before the display exists, as the old harness did.
    bool registered = (FastLED.registerBuffer(frame, PIXELS), true);

public:
    std::string out;
    LedDisplay display{frame};

    // Mirrors LedDisplay::display(): clear the buffer, then render. FastLED applies
    // brightness at show() time, so getBrightness() is the only view of that path.
    void step(const char *label, const uint32_t tick) {
        g_fakeMillis = tick;
        FastLED.clear();
        display.render();
        out += strf("step=%d %s tick=%u brightness=%u\n", stepNo++, label, tick, FastLED.getBrightness());
        bool any = false;
        for (int i = 0; i < PIXELS; i++) {
            const CRGB &c = frame[i];
            if (c.r == 0 && c.g == 0 && c.b == 0) continue;
            out += strf("idx=%d rgb=%u,%u,%u\n", i, c.r, c.g, c.b);
            any = true;
        }
        if (!any) out += "all-black\n";
    }
};

// Blink phases: tick % 500 < 250 is dark, >= 250 is visible.
static const uint32_t VIS = 1300;
static const uint32_t DARK = 1100;

static std::string dumpFrames() {
    FrameScript s;
    LedDisplay &d = s.display;

    // PrefsData's real default, so brightness= reflects startup incl. V1's *0.8 (127 -> 101).
    d.setBrightness(127);

    s.step("initial", VIS);

    d.setNumericValue(7, 42);
    s.step("setNumericValue(7,42) default colours", VIS);

    d.setGlyphsColor(Colors::Red, Colors::Green, Colors::Blue, Colors::Yellow);
    s.step("setGlyphsColor(4)", VIS);

    d.setGlyphsColor(Colors::Pink, Colors::Aqua);
    d.setNumericValue(99, 3);
    s.step("setGlyphsColor(2) setNumericValue(99,3)", VIS);

    d.setGlyphsGlyph(Glyph::P, Glyph::A, Glyph::d, Glyph::E);
    s.step("setGlyphsGlyph(P,A,d,E)", VIS);

    d.setGlyphsAppearance(Colors::Orange, Colors::Violet, true, false);
    s.step("setGlyphsAppearance(Orange,Violet,blinkA) visible", VIS);
    s.step("setGlyphsAppearance(Orange,Violet,blinkA) dark", DARK);

    d.setGlyphBlinking(false, true, false, true);
    s.step("setGlyphBlinking(0,1,0,1) visible", VIS);
    s.step("setGlyphBlinking(0,1,0,1) dark", DARK);

    d.setGlyphBlinking(false, false);
    d.setColonAppearance(Colors::White);
    s.step("setGlyphBlinking(0,0) setColonAppearance(White)", DARK);

    d.setColonAppearance(Colors::Green, true);
    s.step("setColonAppearance(Green,blink) visible", VIS);
    s.step("setColonAppearance(Green,blink) dark", DARK);

    d.setColonAppearance();
    d.setPlayersIndicatorsState(true);
    s.step("setColonAppearance() setPlayersIndicatorsState(true) default colours", VIS);

    d.setSameSideMode(false);
    d.setIndicatorAppearancePlayerA(Colors::Red);
    d.setIndicatorAppearancePlayerB(Colors::Blue, true);
    s.step("sameSide=false indA(Red) indB(Blue,blink) visible", VIS);
    s.step("sameSide=false indA(Red) indB(Blue,blink) dark", DARK);

    d.setSameSideMode(true);
    d.setIndicatorAppearancePlayerA(Colors::Yellow, true);
    d.setIndicatorAppearancePlayerB(Colors::Aqua);
    s.step("sameSide=true indA(Yellow,blink) indB(Aqua) visible", VIS);
    s.step("sameSide=true indA(Yellow,blink) indB(Aqua) dark", DARK);

    d.setPlayersIndicatorsState(false);
    s.step("setPlayersIndicatorsState(false)", VIS);
    d.setSameSideMode(false);
    d.setPlayersIndicatorsState(true);

    std::array<LedBarPixel, LedBar::PIXEL_COUNT> state;
    for (int i = 0; i < LedBar::PIXEL_COUNT; i++) {
        state[i].color = CRGB((uint8_t) (10 * i + 1), (uint8_t) (255 - 7 * i), (uint8_t) (i * i));
        state[i].isBlinking = (i % 3) == 1;
    }
    state[5].color = CRGB::Black;
    state[20].color = CRGB::White;
    d.setLedBarState([&] { return state; });
    s.step("setLedBarState visible", VIS);
    s.step("setLedBarState dark", DARK);

    // The celebration is covered by test_sweep (geometry, not a pixel table).
    d.resetAnimations();
    s.step("resetAnimations", VIS);

    d.setLedBarState([&] { return state; });
    d.setNumericValue(0, 0);
    d.setGlyphsAppearance(Colors::White, Colors::White);
    s.step("setLedBarState after reset, 00:00 White", DARK);

    return s.out;
}

static void test_glyphs_match_golden() {
    assertMatchesGolden("golden_v1_glyphs.txt", dumpGlyphs());
}

static void test_frames_match_golden() {
    assertMatchesGolden("golden_v1_frames.txt", dumpFrames());
}

// V1's OLED is healthy (the battery readout lives in its top strip) and its battery
// factor is the uncalibrated V2 starting value.
static void test_board_facts() {
    TEST_ASSERT_EQUAL_UINT8(0, Board::OLED_DEAD_TOP_ROWS);
    TEST_ASSERT_TRUE(Board::BATTERY_FACTOR > 1.9f && Board::BATTERY_FACTOR < 2.2f);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_glyphs_match_golden);
    RUN_TEST(test_frames_match_golden);
    RUN_TEST(test_board_facts);
    return UNITY_END();
}
