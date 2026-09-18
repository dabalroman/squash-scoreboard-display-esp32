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

/**
 * The menu footer: up to three centred lines. `batteryPercent >= 0` makes line 1
 * the battery icon and that percent, otherwise `line1` is used as plain text;
 * `line2` and `line3` are smaller lines under it, each skipped when empty.
 *
 * One item per line, never two side by side - 128 px does not hold a 12 pt
 * percentage next to a firmware version without them colliding.
 *
 * A default-constructed footer (all null, no battery) means no footer at all, so
 * a menu that wants none simply omits the argument.
 */
struct EInkFooter {
    const char *line1;
    const char *line2;
    const char *line3;
    int16_t batteryPercent;

    EInkFooter() : line1(nullptr), line2(nullptr), line3(nullptr), batteryPercent(-1) {}

    EInkFooter(const char *line1, const char *line2, const char *line3, const int16_t batteryPercent)
        : line1(line1), line2(line2), line3(line3), batteryPercent(batteryPercent) {}
};

#if BOARD_REV == 2

#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include "EInkAsync.h"
#include "EInkWidgets.h"
#include "Fonts/ScoreDigits.h"
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

class EInkDisplay {
public:
    enum : uint16_t { PAPER = EInkAsync::PAPER, INK = EInkAsync::INK };

    EInkDisplay() : eink(Board::EINK_CS, Board::EINK_DC, Board::EINK_RST, Board::EINK_BUSY) {}

    static bool available() { return true; }

    // Blocking (~3 s, boot only): initial full refresh, then the splash is
    // requested as a normal async refresh.
    void begin() {
        // GxEPD2 writes CS/DC/RST before pinMode on them; core 3.x logs
        // "IO n is not set as GPIO". Claiming them first is harmless on 2.0.17.
        constexpr uint8_t pins[] = {Board::EINK_CS, Board::EINK_DC, Board::EINK_RST};
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
     * The match screen: name, score, divider, score, name down the panel, so each
     * player's name sits at their outer edge and the two scores face each other
     * across the label that says what they count.
     *
     * Top half = the player the border's top half shows (left court side). Values
     * are match-level - games won, or gems in padel - never rally points; the
     * divider label says which (GEMY / SETY / TIEBREAK).
     */
    void showMatchScore(const char *topName, const uint8_t topValue,
                        const char *bottomName, const uint8_t bottomValue,
                        const char *label) {
        if (splashHoldActive()) {
            return;
        }

        uint32_t h = hashAdd(HASH_SEED, SCREEN_MATCH);
        h = hashText(h, topName);
        h = hashText(h, bottomName);
        h = hashText(h, label);
        h = hashAdd(h, topValue);
        h = hashAdd(h, bottomValue);
        if (!commit(h)) {
            return;
        }

        GFXcanvas1 &g = eink.gfx();
        g.fillScreen(PAPER);
        g.setTextColor(INK);
        g.setTextWrap(false);

        drawPlayerHalf(g, topName, topValue, MATCH_TOP_SCORE_BASELINE, MATCH_TOP_NAME_BASELINE);

        g.fillRect(0, MATCH_DIVIDER_TOP, g.width(), 2, INK);
        EInkWidgets::printCentered(g, label, MATCH_LABEL_BASELINE, &FreeMonoBold12pt7b);
        g.fillRect(0, MATCH_DIVIDER_BOTTOM, g.width(), 2, INK);

        drawPlayerHalf(g, bottomName, bottomValue, MATCH_BOTTOM_SCORE_BASELINE, MATCH_BOTTOM_NAME_BASELINE);

        present(SCREEN_MATCH);
    }

    /**
     * A free-text screen: inverted title bar plus one large centred line. Used by
     * the device-level Overlay (low battery and, later, the shutdown warning), and
     * value-compared like every other screen, so it is safe to call every frame.
     *
     * `forceFull` turns it into a full refresh, for a screen that must not carry
     * the ghost of what it replaced. It is folded into the hash, or a forced call
     * showing the same title and line as the partial before it is swallowed by the
     * dedup and stays partial.
     */
    void showMessage(const char *title, const char *line, const bool forceFull = false) {
        if (splashHoldActive()) {
            return;
        }

        uint32_t h = hashAdd(HASH_SEED, SCREEN_MESSAGE);
        h = hashText(h, title);
        h = hashText(h, line);
        h = hashAdd(h, forceFull ? 1u : 0u);
        if (!commit(h)) {
            return;
        }

        GFXcanvas1 &g = eink.gfx();
        g.fillScreen(PAPER);
        g.setTextWrap(false);

        EInkWidgets::drawHeader(g, title);

        if (line != nullptr && line[0] != '\0') {
            g.setTextColor(INK);
            EInkWidgets::printCentered(g, line, 170, &FreeMonoBold24pt7b);
        }

        if (forceFull) {
            shownScreen = SCREEN_MESSAGE;
            eink.requestFullRefresh();
            return;
        }

        present(SCREEN_MESSAGE);
    }

    /**
     * A scrolling menu: title bar, rows with the selected one inverted, optional
     * footer. The visible window is this renderer's own state (the OLED keeps its
     * own); it follows the selection and resets when the title changes.
     *
     * Each extra footer line makes the footer taller, but all three heights leave
     * 6 rows visible, so the 6-entry MODE menu and the 5-row CONFIG menu fit
     * without scrolling whatever their footer holds. A 7th MODE entry would start
     * scrolling it.
     */
    void showMenu(const char *title, const EInkMenuRow *rows, const uint8_t rowCount,
                  const uint8_t selected, const EInkFooter &footer = EInkFooter()) {
        if (splashHoldActive()) {
            return;
        }

        const char *extra[2];
        uint8_t extraCount = 0;
        if (hasText(footer.line2)) {
            extra[extraCount++] = footer.line2;
        }
        if (hasText(footer.line3)) {
            extra[extraCount++] = footer.line3;
        }

        const bool hasFooter = footer.batteryPercent >= 0 || hasText(footer.line1) || extraCount > 0;
        constexpr int16_t footerHeights[] = {EInkLayout::FOOTER_HEIGHT, EInkLayout::FOOTER_HEIGHT_TWO,
                                         EInkLayout::FOOTER_HEIGHT_THREE};
        const int16_t footerHeight = hasFooter ? footerHeights[extraCount] : 0;
        const uint8_t visible = visibleRows(footerHeight);

        if (menuTitleHash != hashText(HASH_SEED, title)) {
            menuTitleHash = hashText(HASH_SEED, title);
            menuOffset = 0;
        }
        updateMenuWindow(selected, rowCount, visible);

        uint32_t contentHash = hashAdd(HASH_SEED, SCREEN_MENU);
        contentHash = hashText(contentHash, title);
        contentHash = hashText(contentHash, hasFooter ? footer.line1 : "");
        for (uint8_t i = 0; i < extraCount; i++) {
            contentHash = hashText(contentHash, extra[i]);
        }
        contentHash = hashAdd(contentHash, hasFooter && footer.batteryPercent >= 0 ? 1u : 0u);
        contentHash = hashAdd(contentHash, selected);
        contentHash = hashAdd(contentHash, menuOffset);
        contentHash = hashAdd(contentHash, rowCount);
        for (uint8_t i = 0; i < rowCount; i++) {
            contentHash = hashText(contentHash, rows[i].label);
            contentHash = hashText(contentHash, rows[i].value);
            contentHash = hashAdd(contentHash, static_cast<uint8_t>(rows[i].check));
        }

        const bool contentChanged = (shownScreen != SCREEN_MENU || contentHash != lastMenuContentHash);
        if (contentChanged) {
            lastMenuContentHash = contentHash;
            lastBatteryRefreshMs = millis();
            displayedBatteryPercent = footer.batteryPercent;
        } else if (footer.batteryPercent >= 0 && footer.batteryPercent != displayedBatteryPercent
                   && millis() - lastBatteryRefreshMs >= BATTERY_REFRESH_COOLDOWN_MS) {
            displayedBatteryPercent = footer.batteryPercent;
            lastBatteryRefreshMs = millis();
        }

        const int16_t effectiveBatteryPercent = (footer.batteryPercent >= 0) ? displayedBatteryPercent : -1;

        const uint32_t h = hashAdd(contentHash, static_cast<uint16_t>(hasFooter ? effectiveBatteryPercent : -1));
        if (!commit(h)) {
            return;
        }

        GFXcanvas1 &g = eink.gfx();
        g.fillScreen(PAPER);
        g.setTextWrap(false);

        EInkWidgets::drawHeader(g, title);

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
            EInkWidgets::drawFooter(g, g.height() - footerHeight, footer.line1, extra, extraCount,
                                    effectiveBatteryPercent);
        }

        present(SCREEN_MENU);
    }

    /**
     * A full-screen 128x296 bitmap, same format as the splash. Hashed by pointer
     * identity, not by its 4736 PROGMEM bytes - the content is fixed per header,
     * so the address is the identity, and hashing the bytes every frame is not.
     *
     * Always a full refresh: present() would issue a partial coming off the MODE
     * menu, and a ghosted QR code is one phones fail to decode.
     */
    void showImage(const uint8_t *bitmap) {
        if (splashHoldActive() || bitmap == nullptr) {
            return;
        }

        const uint32_t h = hashAdd(hashAdd(HASH_SEED, SCREEN_IMAGE),
                                   static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bitmap)));
        if (!commit(h)) {
            return;
        }

        GFXcanvas1 &g = eink.gfx();
        g.fillScreen(PAPER);
        g.drawBitmap(0, 0, bitmap, EInkAsync::WIDTH, EInkAsync::HEIGHT, INK);

        shownScreen = SCREEN_IMAGE;
        eink.requestFullRefresh();
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

    // Minimum interval between idle battery-forced e-ink refreshes (5 minutes).
    enum : uint32_t { BATTERY_REFRESH_COOLDOWN_MS = 300000 };

    /**
     * The match screen, in canvas y. Reading down the panel it is name, score,
     * divider, score, name - the names at the two outer edges, the scores facing
     * each other across the label. The divider band stays at 132..164, the
     * panel's vertical centre.
     *
     *       24  name, top edge            288  name, bottom edge
     *   41..120  score (79 px digits)  175..254  score
     *
     * Both name-to-score gaps are 17 px, so the two halves read as mirrored.
     */
    enum : int16_t {
        MATCH_TOP_NAME_BASELINE = 24,
        MATCH_TOP_SCORE_BASELINE = 120,
        MATCH_DIVIDER_TOP = 132,
        MATCH_LABEL_BASELINE = 155,
        MATCH_DIVIDER_BOTTOM = 162,
        MATCH_BOTTOM_SCORE_BASELINE = 254,
        MATCH_BOTTOM_NAME_BASELINE = 288,
    };

    // Screen types for change detection and the ghosting policy.
    enum : uint8_t { SCREEN_NONE = 0, SCREEN_SPLASH, SCREEN_BLANK, SCREEN_MATCH, SCREEN_MENU, SCREEN_MESSAGE, SCREEN_IMAGE };

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

    static bool hasText(const char *text) { return text != nullptr && text[0] != '\0'; }

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
            EInkWidgets::drawTickbox(g, x, boxTop, EInkLayout::CHECKBOX_SIZE, row.check > 0);
            x += EInkLayout::CHECKBOX_SIZE + 5;
        }

        if (hasText(row.value)) {
            EInkWidgets::printRightAligned(g, row.value, g.width() - EInkLayout::TEXT_MARGIN,
                                           baseline, &FreeSans12pt7b);
        }

        // One size, always: an overlong label is clipped at the right edge rather
        // than silently shrinking, which made whole screens look ragged.
        EInkWidgets::printAt(g, row.label, x, baseline, &FreeSans12pt7b);
    }

    // One half of the match screen: the name on its baseline, the score on its own.
    // ScoreDigits is rendered at its real 79 px size, never setTextSize()-scaled,
    // and its widest two digits still fit the 128 px panel - so there is no size
    // fallback and no value that can overflow.
    static void drawPlayerHalf(GFXcanvas1 &g, const char *name, const uint8_t value,
                               const int16_t scoreBaseline, const int16_t nameBaseline) {
        char number[4];
        snprintf(number, sizeof(number), "%u", value);

        EInkWidgets::printCentered(g, number, scoreBaseline, &ScoreDigits);
        EInkWidgets::printCentered(g, name, nameBaseline, &FreeMonoBold12pt7b);
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
    uint32_t lastMenuContentHash = 0;
    int16_t displayedBatteryPercent = -1;
    uint32_t lastBatteryRefreshMs = 0;
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
    void showMatchScore(const char *, const uint8_t, const char *, const uint8_t, const char *) {}
    void showMenu(const char *, const EInkMenuRow *, const uint8_t, const uint8_t,
                  const EInkFooter & = EInkFooter()) {}
    void showMessage(const char *, const char *, const bool = false) {}
    void showImage(const uint8_t *) {}

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
