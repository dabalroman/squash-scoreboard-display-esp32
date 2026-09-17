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
 *             for the waveform; only the ~10 ms SPI bursts per refresh.
 *
 * Screens (showBlank / showMatchScore / showMenu) take plain values and are safe
 * to call every frame: each hashes what it would show and only redraws and
 * queues a refresh when that changed. Refreshes coalesce inside EInkAsync.
 *
 * Views should start their renderEInkDisplay() with
 *   if (!einkDisplay.available()) return;
 * so V1 does not even compute the values for its empty stub.
 */

#include <Arduino.h>
#include <Adafruit_GFX.h>

#include "../../Board.h"

/**
 * One menu row. `value` is drawn right-aligned (nullptr = none). `check` draws a
 * checkbox in front of the label: -1 none, 0 empty, 1 ticked.
 */
struct EInkMenuRow {
    const char *label;
    const char *value;
    int8_t check;
};

#if BOARD_REV == 2

#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "EInkAsync.h"
#include "Images/Splash.h"

static_assert(SPLASH_WIDTH == EInkAsync::WIDTH && SPLASH_HEIGHT == EInkAsync::HEIGHT,
              "The splash image must be a full 128x296 frame");

// Ghosting policy (user, 2026-09-17). A full refresh flashes the panel ~1.6 s but
// never blocks loop(). Any full refresh resets the partial counter.
namespace EInkPolicy {
    // Screen type changes (blank <-> match <-> menu) get a full refresh once this
    // many partials have piled up since the last one.
    constexpr uint32_t TRANSITION_MIN_PARTIALS = 16;
    // Hard limit: the next refresh is full, whatever is on screen.
    constexpr uint32_t MAX_PARTIALS = 128;
}

// E-paper layout, 128 x 296 portrait. Its own constants - nothing from the OLED.
namespace EInkLayout {
    constexpr int16_t TITLE_HEIGHT = 30;
    constexpr int16_t ROWS_TOP = 36;
    constexpr int16_t ROW_PITCH = 29;
    constexpr int16_t ROW_BASELINE = 21;     // from the row top, FreeSans12pt
    constexpr int16_t FOOTER_HEIGHT = 26;        // one 12 pt line
    constexpr int16_t FOOTER_HEIGHT_TWO = 44;    // two 9 pt lines (e.g. battery + firmware)
    constexpr int16_t CHECKBOX_SIZE = 13;
    constexpr int16_t SELECTION_BORDER = 2;  // outline thickness of the selected row
    constexpr int16_t TEXT_MARGIN = 5;
}

class EInkDisplay {
public:
    enum : uint16_t { PAPER = EInkAsync::PAPER, INK = EInkAsync::INK };

    EInkDisplay() : eink(Board::EINK_CS, Board::EINK_DC, Board::EINK_RST, Board::EINK_BUSY) {}

    bool available() const { return true; }

    // Blocking (~3 s, boot only): initial full refresh, then the splash is
    // requested as a normal async refresh.
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

    /**
     * setup() only: drive a queued refresh out now, for the window before loop()
     * starts pumping update(). Bounded so a stuck BUSY cannot hang boot.
     */
    void flushRefresh() {
        const uint32_t start = millis();

        while (eink.hasWork() && millis() - start < FLUSH_TIMEOUT_MS) {
            eink.update();
        }
    }

    /**
     * End the splash hold early. main.cpp calls this on any accepted remote press,
     * so a button skips the boot image; the press still does its normal job.
     */
    void dismissSplash() { splashHoldUntilMs = 0; }

    void showBlank() {
        if (splashHoldActive()) {
            return;
        }
        if (!commit(hashAdd(HASH_SEED, SCREEN_BLANK))) {
            return;
        }
        eink.gfx().fillScreen(PAPER);
        present(SCREEN_BLANK);
    }

    /**
     * The match screen: top row = the player the border's top half shows (left
     * court side), bottom row = the other. Values are match-level - games won, or
     * gems in padel - never rally points. Sets < 0 hides the sets line.
     */
    void showMatchScore(const char *topName, const uint8_t topValue,
                        const char *bottomName, const uint8_t bottomValue,
                        const char *label, const int16_t topSets = -1, const int16_t bottomSets = -1) {
        if (splashHoldActive()) {
            return;
        }

        uint32_t h = hashAdd(HASH_SEED, SCREEN_MATCH);
        h = hashText(h, topName);
        h = hashText(h, bottomName);
        h = hashText(h, label);
        h = hashAdd(h, topValue);
        h = hashAdd(h, bottomValue);
        h = hashAdd(h, static_cast<uint16_t>(topSets));
        h = hashAdd(h, static_cast<uint16_t>(bottomSets));
        if (!commit(h)) {
            return;
        }

        GFXcanvas1 &g = eink.gfx();
        g.fillScreen(PAPER);
        g.setTextColor(INK);
        g.setTextWrap(false);

        drawPlayerBlock(g, 0, topName, topValue, topSets);

        g.fillRect(0, 132, g.width(), 2, INK);
        printCentered(g, label, 155, &FreeMonoBold12pt7b);
        g.fillRect(0, 162, g.width(), 2, INK);

        drawPlayerBlock(g, 164, bottomName, bottomValue, bottomSets);

        present(SCREEN_MATCH);
    }

    /**
     * A free-text screen: inverted title bar plus one large centred line. Used by
     * the device-level Overlay (low battery and, later, the shutdown warning), and
     * value-compared like every other screen, so it is safe to call every frame.
     */
    void showMessage(const char *title, const char *line) {
        if (splashHoldActive()) {
            return;
        }

        uint32_t h = hashAdd(HASH_SEED, SCREEN_MESSAGE);
        h = hashText(h, title);
        h = hashText(h, line);
        if (!commit(h)) {
            return;
        }

        GFXcanvas1 &g = eink.gfx();
        g.fillScreen(PAPER);
        g.setTextWrap(false);

        g.fillRect(0, 0, g.width(), EInkLayout::TITLE_HEIGHT, INK);
        g.setTextColor(PAPER);
        printFitted(g, title, 22);

        if (line != nullptr && line[0] != '\0') {
            g.setTextColor(INK);
            printCentered(g, line, 170, &FreeMonoBold24pt7b);
        }

        present(SCREEN_MESSAGE);
    }

    /**
     * A scrolling menu: title bar, rows with the selected one inverted, optional
     * footer. The visible window is this renderer's own state (the OLED keeps its
     * own); it follows the selection and resets when the title changes.
     *
     * A second footer line (only with the first) switches the footer to two 9 pt
     * lines instead of one 12 pt line - 128 px is too narrow to hold both on one.
     * Either height leaves 7 rows visible, so no menu re-flows.
     */
    void showMenu(const char *title, const EInkMenuRow *rows, const uint8_t rowCount,
                  const uint8_t selected, const char *footer = nullptr,
                  const char *footerSecondary = nullptr) {
        if (splashHoldActive()) {
            return;
        }

        const bool hasFooter = footer != nullptr && footer[0] != '\0';
        const bool hasSecondary = hasFooter && footerSecondary != nullptr && footerSecondary[0] != '\0';
        const int16_t footerHeight = !hasFooter
                                         ? 0
                                         : (hasSecondary ? EInkLayout::FOOTER_HEIGHT_TWO : EInkLayout::FOOTER_HEIGHT);
        const uint8_t visible = visibleRows(footerHeight);

        if (menuTitleHash != hashText(HASH_SEED, title)) {
            menuTitleHash = hashText(HASH_SEED, title);
            menuOffset = 0;
        }
        updateMenuWindow(selected, rowCount, visible);

        uint32_t h = hashAdd(HASH_SEED, SCREEN_MENU);
        h = hashText(h, title);
        h = hashText(h, hasFooter ? footer : "");
        h = hashText(h, hasSecondary ? footerSecondary : "");
        h = hashAdd(h, selected);
        h = hashAdd(h, menuOffset);
        h = hashAdd(h, rowCount);
        for (uint8_t i = 0; i < rowCount; i++) {
            h = hashText(h, rows[i].label);
            h = hashText(h, rows[i].value);
            h = hashAdd(h, static_cast<uint8_t>(rows[i].check));
        }
        if (!commit(h)) {
            return;
        }

        GFXcanvas1 &g = eink.gfx();
        g.fillScreen(PAPER);
        g.setTextWrap(false);

        // Title bar, inverted.
        g.fillRect(0, 0, g.width(), EInkLayout::TITLE_HEIGHT, INK);
        g.setTextColor(PAPER);
        printCentered(g, title, 22, &FreeSansBold12pt7b);

        for (uint8_t row = 0; row < visible && menuOffset + row < rowCount; row++) {
            const uint8_t index = menuOffset + row;
            drawMenuRow(g, EInkLayout::ROWS_TOP + row * EInkLayout::ROW_PITCH, rows[index], index == selected);
        }

        // Scroll hints when rows are hidden above/below.
        if (menuOffset > 0) {
            g.fillTriangle(g.width() / 2 - 5, EInkLayout::ROWS_TOP - 1, g.width() / 2 + 5,
                           EInkLayout::ROWS_TOP - 1, g.width() / 2, EInkLayout::ROWS_TOP - 4, INK);
        }
        const int16_t rowsBottom = EInkLayout::ROWS_TOP + visible * EInkLayout::ROW_PITCH;
        if (menuOffset + visible < rowCount) {
            g.fillTriangle(g.width() / 2 - 5, rowsBottom + 1, g.width() / 2 + 5, rowsBottom + 1,
                           g.width() / 2, rowsBottom + 4, INK);
        }

        if (hasFooter) {
            const int16_t top = g.height() - footerHeight;
            g.fillRect(0, top, g.width(), 2, INK);
            g.setTextColor(INK);

            if (hasSecondary) {
                printCentered(g, footer, top + 17, &FreeSans9pt7b);
                printCentered(g, footerSecondary, top + 37, &FreeSans9pt7b);
            } else {
                printCentered(g, footer, top + 20, &FreeSans12pt7b);
            }
        }

        present(SCREEN_MENU);
    }

    uint32_t refreshes() const { return eink.getStats().refreshes; }
    uint32_t fullRefreshes() const { return eink.getStats().fullRefreshes; }
    uint32_t partialsSinceFull() const { return eink.partialsSinceFull(); }
    uint32_t worstStartUs() const { return eink.getStats().worstStartUs; }
    uint32_t worstFinishUs() const { return eink.getStats().worstFinishUs; }
    uint32_t timeouts() const { return eink.getStats().timeouts; }
    uint32_t busyNeverRose() const { return eink.getStats().busyNeverRose; }

private:
    enum : uint32_t { HASH_SEED = 2166136261u };   // FNV-1a offset basis

    // How long the boot splash keeps the panel before any view may draw (user, 2026-09-17).
    enum : uint32_t { SPLASH_HOLD_MS = 4000 };

    // Upper bound for flushRefresh(); a partial is ~0.5 s, a full ~1.6 s.
    enum : uint32_t { FLUSH_TIMEOUT_MS = 5000 };

    // Screen types for change detection and the ghosting policy.
    enum : uint8_t { SCREEN_NONE = 0, SCREEN_SPLASH, SCREEN_BLANK, SCREEN_MATCH, SCREEN_MENU, SCREEN_MESSAGE };

    // True if `hash` differs from what is on the panel; the caller then redraws.
    bool commit(const uint32_t hash) {
        if (hasContent && hash == lastContentHash) {
            return false;
        }
        hasContent = true;
        lastContentHash = hash;
        return true;
    }

    // Queue the drawn canvas: full refresh on a screen-type change once partials
    // piled up, or at the hard limit; partial otherwise.
    void present(const uint8_t screen) {
        const bool transition = screen != shownScreen;
        shownScreen = screen;

        const uint32_t partials = eink.partialsSinceFull();
        if (partials >= EInkPolicy::MAX_PARTIALS
            || (transition && partials >= EInkPolicy::TRANSITION_MIN_PARTIALS)) {
            eink.requestFullRefresh();
        } else {
            eink.requestRefresh();
        }
    }

    // FNV-1a over the 4 bytes of `value`. Cheap, order-sensitive.
    static uint32_t hashAdd(uint32_t hash, const uint32_t value) {
        for (uint8_t i = 0; i < 4; i++) {
            hash ^= (value >> (i * 8)) & 0xFFu;
            hash *= 16777619u;
        }
        return hash;
    }

    static uint32_t hashText(uint32_t hash, const char *text) {
        for (const char *c = text; c != nullptr && *c != '\0'; c++) {
            hash = hashAdd(hash, static_cast<uint8_t>(*c));
        }
        return hashAdd(hash, 0);
    }

    static uint8_t visibleRows(const int16_t footerHeight) {
        const int16_t bottom = 296 - footerHeight - 6;   // room for the scroll hint
        return static_cast<uint8_t>((bottom - EInkLayout::ROWS_TOP) / EInkLayout::ROW_PITCH);
    }

    // True while the boot splash still owns the panel; the screens return early so
    // the canvas keeps the image and no hashing or window state advances.
    bool splashHoldActive() {
        if (splashHoldUntilMs == 0) {
            return false;
        }
        if (static_cast<int32_t>(millis() - splashHoldUntilMs) >= 0) {
            splashHoldUntilMs = 0;
            return false;
        }
        return true;
    }

    // Move the window only as far as needed to keep the selection visible.
    void updateMenuWindow(const uint8_t selected, const uint8_t rowCount, const uint8_t visible) {
        if (selected >= menuOffset + visible) {
            menuOffset = selected - visible + 1;
        }
        if (selected < menuOffset) {
            menuOffset = selected;
        }
        if (rowCount <= visible) {
            menuOffset = 0;
        } else if (menuOffset > rowCount - visible) {
            menuOffset = rowCount - visible;
        }
    }

    static int16_t textWidth(GFXcanvas1 &g, const char *text, const GFXfont *font) {
        g.setFont(font);
        g.setTextSize(1);
        int16_t x1, y1;
        uint16_t w, h;
        g.getTextBounds(text, 0, 100, &x1, &y1, &w, &h);
        return static_cast<int16_t>(w);
    }

    static void printCentered(GFXcanvas1 &g, const char *text, const int16_t baseline,
                              const GFXfont *font, const uint8_t size = 1) {
        g.setFont(font);
        g.setTextSize(size);
        int16_t x1, y1;
        uint16_t w, h;
        g.getTextBounds(text, 0, baseline, &x1, &y1, &w, &h);
        g.setCursor((g.width() - static_cast<int16_t>(w)) / 2 - x1, baseline);
        g.print(text);
    }

    // Title text wider than the panel drops to 9 pt rather than being clipped.
    static void printFitted(GFXcanvas1 &g, const char *text, const int16_t baseline) {
        const int16_t room = g.width() - 2 * EInkLayout::TEXT_MARGIN;
        const GFXfont *font = textWidth(g, text, &FreeSansBold12pt7b) <= room
                                  ? &FreeSansBold12pt7b
                                  : &FreeSans9pt7b;
        printCentered(g, text, baseline, font);
    }

    // Menu rows use FreeSans; the selected row gets an outline, not a fill.
    static void drawMenuRow(GFXcanvas1 &g, const int16_t top, const EInkMenuRow &row, const bool isSelected) {
        if (isSelected) {
            for (int16_t i = 0; i < EInkLayout::SELECTION_BORDER; i++) {
                g.drawRoundRect(i, top + i, g.width() - 2 * i, EInkLayout::ROW_PITCH - 2 * i, 4, INK);
            }
        }
        g.setTextColor(INK);

        const int16_t baseline = top + EInkLayout::ROW_BASELINE;
        int16_t x = EInkLayout::TEXT_MARGIN;

        if (row.check >= 0) {
            const int16_t boxTop = top + (EInkLayout::ROW_PITCH - EInkLayout::CHECKBOX_SIZE) / 2;
            g.drawRect(x, boxTop, EInkLayout::CHECKBOX_SIZE, EInkLayout::CHECKBOX_SIZE, INK);
            if (row.check > 0) {
                g.fillRect(x + 3, boxTop + 3, EInkLayout::CHECKBOX_SIZE - 6, EInkLayout::CHECKBOX_SIZE - 6, INK);
            }
            x += EInkLayout::CHECKBOX_SIZE + 5;
        }

        int16_t valueWidth = 0;
        if (row.value != nullptr && row.value[0] != '\0') {
            valueWidth = textWidth(g, row.value, &FreeSans12pt7b);
            g.setFont(&FreeSans12pt7b);
            g.setCursor(g.width() - EInkLayout::TEXT_MARGIN - valueWidth, baseline);
            g.print(row.value);
            valueWidth += 6;
        }

        // Long labels (e.g. an IP address) fall back to the 9 pt size.
        const int16_t room = g.width() - EInkLayout::TEXT_MARGIN - valueWidth - x;
        const GFXfont *font = textWidth(g, row.label, &FreeSans12pt7b) <= room ? &FreeSans12pt7b : &FreeSans9pt7b;
        g.setFont(font);
        g.setCursor(x, baseline);
        g.print(row.label);
    }

    // One 132 px tall player block starting at y: name, big value, optional sets.
    static void drawPlayerBlock(GFXcanvas1 &g, const int16_t y, const char *name,
                                const uint8_t value, const int16_t sets) {
        printCentered(g, name, y + 22, &FreeMonoBold12pt7b);

        char number[4];
        snprintf(number, sizeof(number), "%u", value);
        printCentered(g, number, y + 96, &FreeMonoBold24pt7b, 2);   // ~62 px tall digits

        if (sets >= 0) {
            char line[12];
            snprintf(line, sizeof(line), "SETS %d", sets);
            printCentered(g, line, y + 124, &FreeMonoBold9pt7b);
        }
    }

    /**
     * The boot image, a full 128x296 frame from helpers/eink_image.py. The canvas
     * is at rotation 2, so the bitmap is authored upright as it is seen on the
     * device. The hold that follows keeps it readable - without it the MODE menu
     * lands ~0.5 s later, as soon as this refresh finishes.
     */
    void drawSplash() {
        GFXcanvas1 &g = eink.gfx();

        g.fillScreen(PAPER);
        g.drawBitmap(0, 0, SPLASH_BITMAP, SPLASH_WIDTH, SPLASH_HEIGHT, INK);

        commit(hashAdd(HASH_SEED, SCREEN_SPLASH));
        present(SCREEN_SPLASH);

        splashHoldUntilMs = millis() + SPLASH_HOLD_MS;
    }

    EInkAsync eink;
    bool hasContent = false;
    uint32_t lastContentHash = 0;
    uint8_t shownScreen = SCREEN_NONE;
    uint32_t menuTitleHash = 0;
    uint8_t menuOffset = 0;
    uint32_t splashHoldUntilMs = 0;
};

#else

// V1 has no e-paper. Same API, all empty; nothing is allocated.
class EInkDisplay {
public:
    bool available() const { return false; }
    void begin() {}
    void update() {}
    void flushRefresh() {}
    void dismissSplash() {}

    void showBlank() {}
    void showMatchScore(const char *, const uint8_t, const char *, const uint8_t,
                        const char *, const int16_t = -1, const int16_t = -1) {}
    void showMenu(const char *, const EInkMenuRow *, const uint8_t, const uint8_t,
                  const char * = nullptr, const char * = nullptr) {}
    void showMessage(const char *, const char *) {}

    uint32_t refreshes() const { return 0; }
    uint32_t fullRefreshes() const { return 0; }
    uint32_t partialsSinceFull() const { return 0; }
    uint32_t worstStartUs() const { return 0; }
    uint32_t worstFinishUs() const { return 0; }
    uint32_t timeouts() const { return 0; }
    uint32_t busyNeverRose() const { return 0; }
};

#endif

#endif //EINK_DISPLAY_H
