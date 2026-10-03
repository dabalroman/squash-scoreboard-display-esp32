// Host stand-ins for what the views include but the LED shim does not carry: the OLED,
// the e-paper, the OLED menu widget, the logger and the NVS roster. Each pre-defines the
// real header's include guard, so it must be included before any view or mode.
#ifndef TEST_GARMIN_VIEWS_STUBS_H
#define TEST_GARMIN_VIEWS_STUBS_H

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#include <Arduino.h>

// --- Arduino core bits beyond the LED shim -------------------------------------------
#ifndef HIGH
#define HIGH 1
#define LOW 0
#endif
#ifndef PROGMEM
#define PROGMEM
#endif

inline int digitalRead(uint8_t) { return LOW; }
inline uint16_t analogRead(uint8_t) { return 0; }
inline uint32_t analogReadMilliVolts(uint8_t) { return 0; }
enum { ADC_11db = 3 };
inline void analogSetPinAttenuation(uint8_t, int) {}

class String {
    std::string s;

public:
    String(const char *c = "") : s(c ? c : "") {}
    explicit String(const char c) : s(1, c) {}
    explicit String(const unsigned char v) : s(std::to_string(static_cast<unsigned>(v))) {}
    explicit String(const int v) : s(std::to_string(v)) {}
    explicit String(const unsigned v) : s(std::to_string(v)) {}

    const char *c_str() const { return s.c_str(); }
    size_t length() const { return s.size(); }
    bool operator==(const String &o) const { return s == o.s; }
};

// --- Logger ----------------------------------------------------------------------------
#define LOGGER_HELPER
inline void printLn(const char *, ...) {}

// --- NVS roster: the views only need its limits ------------------------------------------
#define PLAYER_ROSTER_H
#include "PlayerRosterData.h"

// --- OLED ------------------------------------------------------------------------------
#define BACK_DISPLAY_H
class BackDisplay {
public:
    void clear() {}
    void display() {}
    void initSmallFont() {}
    void initBigFont() {}
    void setSameSideMode(bool) {}
    void setWatchCount(uint8_t) {}
    void drawBitmap(const uint8_t *) {}
    void drawBatteryPercent(uint8_t) {}
    void drawWatchBadge() {}
    template <typename L, typename R> void renderPlayerWidget(const L &, const R &) {}
    template <typename L, typename R> void renderScoreWidget(const L &, const R &) {}
};

#define SCROLLABLEWIDGET_H
#include "Display/Scrollable.h"
class ScrollableWidget {
public:
    explicit ScrollableWidget(Scrollable &) {}
    void render(BackDisplay &) {}
};

// --- E-paper: V1's stub API on both boards ---------------------------------------------
#define EINK_DISPLAY_H
struct EInkMenuRow {
    const char *label;
    const char *value;
    int8_t check;
};

struct EInkFooter {
    const char *line1;
    const char *line2;
    const char *line3;
    int16_t batteryPercent;

    EInkFooter() : line1(nullptr), line2(nullptr), line3(nullptr), batteryPercent(-1) {}

    EInkFooter(const char *line1, const char *line2, const char *line3, const int16_t batteryPercent)
        : line1(line1), line2(line2), line3(line3), batteryPercent(batteryPercent) {}
};

class EInkDisplay {
public:
    static bool available() { return false; }
    void showBlank() {}
    void showMatchScore(const char *, uint8_t, const char *, uint8_t, const char *) {}
    void showMenu(const char *, const EInkMenuRow *, uint8_t, uint8_t, const EInkFooter & = EInkFooter()) {}
    void showImage(const uint8_t *) {}
};

#endif
