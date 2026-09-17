#ifndef LED_STARTUP_ANIMATION_H
#define LED_STARTUP_ANIMATION_H

#include <Arduino.h>
#include <FastLED.h>
#include <math.h>

#include "Board.h"

/**
 * V2: the boot sweep. A thin rainbow ring grows from the centre of the e-paper
 * outward across the front LEDs in ~1 s, then the strip goes dark. Played
 * blocking from setup(), after einkDisplay.begin(), so it runs against the
 * e-paper splash rather than a blank panel.
 *
 * Physical positions come from assets/led-map.svg; helpers/led_positions.py
 * regenerates the table below (and `--check` verifies it still matches).
 * Units are raw map units, origin = e-paper centre, +x right, +y down
 * (~16 units per mm, so the 197 between adjacent LEDs is ~12 mm).
 *
 * Only front LEDs are animated. Slots 4 and 9 are the back-facing indicators and
 * slot 2 of each module (absolute 12, 28, 44, 60) is an unfitted chain position:
 * both are marked SKIP and never written.
 *
 * V1 has no startup animation: an empty stub with the same API.
 */

#if BOARD_REV == 2

class LedStartupAnimation {
    CRGB *pixels;

    /** 6-sector HSV at full S/V. Hand-rolled: the project keeps FastLED's colour
     *  engine unused (no CHSV) so FastLED stays replaceable. */
    static CRGB hueToRgb(const uint8_t hue) {
        const uint8_t sector = hue / 43;
        const uint8_t rise = static_cast<uint8_t>((hue - sector * 43) * 6);
        const uint8_t fall = static_cast<uint8_t>(255 - rise);

        switch (sector) {
            case 0: return CRGB(255, rise, 0);
            case 1: return CRGB(fall, 255, 0);
            case 2: return CRGB(0, 255, rise);
            case 3: return CRGB(0, fall, 255);
            case 4: return CRGB(rise, 0, 255);
            default: return CRGB(255, 0, fall);
        }
    }

public:
    enum : int16_t { SKIP = -32768 };
    enum : uint16_t { DURATION_MS = 1000, FRAME_DELAY_MS = 10, REPEAT_GAP_MS = 300 };

    explicit LedStartupAnimation(CRGB *pixels) : pixels(pixels) {
    }

    /** Clock-free, so check_v2 can step it frame by frame. */
    void renderFrame(const uint32_t elapsedMs) const {
        // Function-local: static constexpr arrays as class members hit ODR errors on GCC 8.4.
        static constexpr int16_t POS[Board::LED_COUNT][2] = {
            {  -342,   295},  //  0  border left,  bottom outer
            {  -342,    98},  //  1  border left,  bottom inner
            {  -342,   -98},  //  2  border left,  top inner
            {  -342,  -295},  //  3  border left,  top outer
            {  SKIP,     0},  //  4  back indicator B
            {   331,   295},  //  5  border right, bottom outer
            {   331,    98},  //  6  border right, bottom inner
            {   331,   -98},  //  7  border right, top inner
            {   331,  -295},  //  8  border right, top outer
            {  SKIP,     0},  //  9  back indicator A
            {  2203,   377},  // 10  digit D
            {  2203,   180},  // 11
            {  SKIP,     0},  // 12  dead slot 2
            {  2203,  -214},  // 13
            {  2203,  -411},  // 14
            {  2020,  -606},  // 15
            {  1823,  -606},  // 16
            {  1626,  -606},  // 17
            {  1662,  -214},  // 18
            {  1662,   -17},  // 19
            {  1662,   180},  // 20
            {  1626,   573},  // 21
            {  1823,   573},  // 22
            {  2020,   573},  // 23
            {  2097,   -16},  // 24
            {  1900,   -16},  // 25
            {  1245,   377},  // 26  digit C
            {  1245,   180},  // 27
            {  SKIP,     0},  // 28  dead slot 2
            {  1245,  -214},  // 29
            {  1245,  -411},  // 30
            {  1062,  -606},  // 31
            {   865,  -606},  // 32
            {   668,  -606},  // 33
            {   705,  -214},  // 34
            {   705,   -17},  // 35
            {   705,   180},  // 36
            {   668,   573},  // 37
            {   865,   573},  // 38
            {  1062,   573},  // 39
            {  1139,   -16},  // 40
            {   942,   -16},  // 41
            {  -717,   376},  // 42  digit B
            {  -717,   179},  // 43
            {  SKIP,     0},  // 44  dead slot 2
            {  -717,  -215},  // 45
            {  -717,  -412},  // 46
            {  -900,  -607},  // 47
            { -1097,  -607},  // 48
            { -1294,  -607},  // 49
            { -1258,  -215},  // 50
            { -1258,   -18},  // 51
            { -1258,   179},  // 52
            { -1294,   572},  // 53
            { -1097,   572},  // 54
            {  -900,   572},  // 55
            {  -823,   -17},  // 56
            { -1020,   -17},  // 57
            { -1675,   376},  // 58  digit A
            { -1675,   179},  // 59
            {  SKIP,     0},  // 60  dead slot 2
            { -1675,  -215},  // 61
            { -1675,  -412},  // 62
            { -1858,  -607},  // 63
            { -2055,  -607},  // 64
            { -2252,  -607},  // 65
            { -2215,  -215},  // 66
            { -2215,   -18},  // 67
            { -2215,   179},  // 68
            { -2252,   572},  // 69
            { -2055,   572},  // 70
            { -1858,   572},  // 71
            { -1781,   -17},  // 72
            { -1978,   -17},  // 73
        };

        // BAND is two LED pitches: the field has radial gaps (empty centre, border-to-digit)
        // that a thinner ring falls into, blanking whole frames. check_v2 guards it.
        constexpr float MAX_RADIUS = 2332.0f;   // farthest die, slot 65
        constexpr float BAND = 394.0f;
        constexpr float SWEEP_LEN = MAX_RADIUS + BAND;
        constexpr float HUE_SPAN = 200.0f;   // not 255: the edge must not wrap back to red

        const uint32_t clamped = elapsedMs > DURATION_MS ? DURATION_MS : elapsedMs;
        const float radius = SWEEP_LEN * static_cast<float>(clamped) / static_cast<float>(DURATION_MS);

        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            if (POS[slot][0] == SKIP) {
                continue;
            }

            const float x = static_cast<float>(POS[slot][0]);
            const float y = static_cast<float>(POS[slot][1]);
            const float distance = sqrtf(x * x + y * y);
            const float intensity = 1.0f - fabsf(distance - radius) / BAND;

            if (intensity <= 0.0f) {
                pixels[slot] = CRGB::Black;
                continue;
            }

            const CRGB hue = hueToRgb(static_cast<uint8_t>(distance / MAX_RADIUS * HUE_SPAN));
            pixels[slot] = CRGB(
                static_cast<uint8_t>(hue.r * intensity),
                static_cast<uint8_t>(hue.g * intensity),
                static_cast<uint8_t>(hue.b * intensity)
            );
        }
    }

    /** Blocking. repeats > 1 is a bench aid: REPEAT_GAP_MS of dark keeps the sweeps distinct. */
    void play(const uint8_t repeats = 1) const {
        for (uint8_t run = 0; run < repeats; run++) {
            if (run > 0) {
                delay(REPEAT_GAP_MS);
            }

            const uint32_t start = millis();

            for (uint32_t elapsed = 0; elapsed < DURATION_MS; elapsed = millis() - start) {
                renderFrame(elapsed);
                FastLED.show();
                delay(FRAME_DELAY_MS);
            }

            FastLED.clear();
            FastLED.show();
        }
    }
};

#else

class LedStartupAnimation {
public:
    enum : int16_t { SKIP = -32768 };
    enum : uint16_t { DURATION_MS = 0, FRAME_DELAY_MS = 0, REPEAT_GAP_MS = 0 };

    explicit LedStartupAnimation(CRGB *) {
    }

    void renderFrame(const uint32_t) const {
    }

    void play(const uint8_t = 1) const {
    }
};

#endif

#endif //LED_STARTUP_ANIMATION_H
