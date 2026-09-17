#ifndef EINK_ASYNC_H
#define EINK_ASYNC_H

// Non-blocking e-paper driver for the V2 panel (WeAct 2.9", SSD1680, 128x296),
// for a single-loop program with no custom FreeRTOS tasks. V2 only: include it
// through EInkDisplay.h, never directly.
//
// Ported from the rig (squash-scoreboard-display-testing-playground,
// eink_async/EInkAsync.h), where it was verified on V2 hardware with core 2.0.17.
// Behaviour is unchanged; only the write-mode variants used for benchmarking were
// dropped (Bulk with the GxEPD2 fallback is kept) and the stats were trimmed.
//
// GxEPD2 does a partial update as: write image to RAM 0x24 -> send update
// command -> BLOCK ~475 ms on BUSY -> write image again to 0x26/0x24 (the
// "previous" frame the next differential partial compares against). The panel
// controller runs the waveform on its own; the ESP only watches a pin. So this
// splits the update at the wait and lets loop() poll BUSY:
//
//   Idle --request--> [SPI: 0x24 + commands] --> WaitBusyHigh
//   WaitBusyHigh --BUSY seen high--> WaitBusyLow
//   WaitBusyLow  --BUSY low--> [SPI: 0x26] --> Idle
//
// A full refresh (clears ghosting, flashes ~1.6 s) uses the same wait states:
//
//   Idle --full request--> [SPI: 0x26] --> FullWriteCurrent
//   FullWriteCurrent --next pass--> [SPI: 0x24 + full update commands] --> WaitBusyHigh
//
// The two RAM writes are split across two loop() passes so no pass blocks longer
// than one ~10 ms burst.
//
// Rules it enforces:
//   * Nothing is sent to the panel while a refresh is in flight.
//   * Requests during a refresh coalesce: only the latest canvas is sent, once.
//   * The finish step sends the image that was DISPLAYED (a snapshot), not the
//     canvas, which the caller may already have redrawn.
//   * Never finish before BUSY was seen HIGH (50 ms grace), 5 s timeout.
//
// Always full-screen partials: area barely changes the time on this panel
// (~45 ms, all SPI), and it keeps the 0x26 "previous" RAM trivially consistent.
//
// Finish writes only 0x26. GxEPD2's writeImageAgain() also rewrites 0x24, but for
// full-screen updates that is redundant by construction: the next start step
// overwrites all of 0x24 anyway, and only 0x26 feeds the next differential update.
//
// Bulk SPI: GxEPD2's _writeImage() sends 4,736 single-byte transfers with two
// delay(1) calls around them. The bulk path sends the same bytes in one
// SPI.writeBytes() call (~20 ms -> ~10 ms per burst).

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>

// Adds what GxEPD2 lacks: starting a partial refresh without waiting, and a
// full-frame RAM write without per-byte overhead. _Update_Part() and
// _setPartialRamArea() are private in the base, so the same command bytes are
// re-sent here (GxEPD2 1.6.9, GxEPD2_290_GDEY029T94.cpp - pinned in platformio.ini).
class NonBlockingPanel : public GxEPD2_290_GDEY029T94 {
public:
    // Enum, not static constexpr: no out-of-line definition needed in C++11.
    enum : size_t { FRAME_BYTES = (size_t) WIDTH * HEIGHT / 8 };   // 4736

    NonBlockingPanel(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
        : GxEPD2_290_GDEY029T94(cs, dc, rst, busy) {}

    bool needsInitialFullRefresh() const { return _initial_refresh; }

    // The bulk path skips GxEPD2's lazy init and first-write clear, so it is only
    // valid once a normal write has done both (begin() guarantees that).
    bool readyForBulkWrites() const { return _init_display_done && !_initial_write; }

    bool busyPinActive() const {
        return _busy >= 0 && digitalRead(_busy) == _busy_level;
    }

    // Whole frame into RAM 0x24 (current) or 0x26 (previous), one SPI burst.
    // Same bytes as GxEPD2's _writeImage(command, frame, 0, 0, WIDTH, HEIGHT).
    void writeRamBulk(const uint8_t command, const uint8_t *frame) {
        setFullRamWindow();
        _writeCommand(command);
        _startTransfer();   // beginTransaction + CS low; DC stays high = data
        _pSPIx->writeBytes(frame, FRAME_BYTES);
        _endTransfer();
    }

    // refresh(false) -> _Update_Full(), minus _waitWhileBusy(). Fast full waveform:
    // 100 °C written to the temperature register (useFastFullUpdate in GxEPD2).
    void startFullUpdate() {
        _writeCommand(0x1a);   // temperature register
        _writeData(0x64);      // 100 °C -> fast full-refresh waveform
        _writeCommand(0x22);   // display update control 2
        _writeData(0xd7);      // clock + analog on, load LUT, display, power off
        _writeCommand(0x20);   // master activation - returns immediately
        _power_is_on = false;
        _initial_refresh = false;
    }

    // refresh(0, 0, WIDTH, HEIGHT) + _Update_Part(), minus _waitWhileBusy().
    void startFullScreenPartial() {
        setFullRamWindow();
        _writeCommand(0x22);   // display update control 2
        _writeData(0xfc);      // clock + analog on, load OTP partial waveform, display
        _writeCommand(0x20);   // master activation - returns immediately
        _power_is_on = true;
    }

private:
    // _setPartialRamArea(0, 0, WIDTH, HEIGHT)
    void setFullRamWindow() {
        _writeCommand(0x11);   // data entry mode
        _writeData(0x03);      // x increase, y increase
        _writeCommand(0x44);   // RAM x window
        _writeData(0x00);
        _writeData((WIDTH - 1) / 8);
        _writeCommand(0x45);   // RAM y window
        _writeData(0x00);
        _writeData(0x00);
        _writeData((HEIGHT - 1) % 256);
        _writeData((HEIGHT - 1) / 256);
        _writeCommand(0x4e);   // RAM x counter
        _writeData(0x00);
        _writeCommand(0x4f);   // RAM y counter
        _writeData(0x00);
        _writeData(0x00);
    }
};

class EInkAsync {
public:
    // Native (rotation 0) panel size.
    enum : uint16_t { WIDTH = 128, HEIGHT = 296 };
    enum : size_t { FRAME_BYTES = NonBlockingPanel::FRAME_BYTES };
    static_assert(GxEPD2_290_GDEY029T94::WIDTH == 128 && GxEPD2_290_GDEY029T94::HEIGHT == 296,
                  "EInkAsync assumes the 128x296 panel");

    // GFXcanvas1 colours: 1 = white, 0 = black (matches SSD1680 RAM: bit set = white).
    // Not named WHITE/BLACK: Adafruit_SSD1306.h #defines those as global macros.
    enum : uint16_t { PAPER = 1, INK = 0 };

    struct Stats {
        uint32_t refreshes = 0;       // partials actually completed
        uint32_t fullRefreshes = 0;   // async full refreshes completed (boot one excluded)
        uint32_t worstStartUs = 0;    // blocking SPI at start of a cycle
        uint32_t worstFinishUs = 0;   // blocking SPI at end of a cycle
        uint32_t busyNeverRose = 0;   // BUSY did not go high within the grace window
        uint32_t timeouts = 0;        // BUSY stuck high past the timeout
    };

    EInkAsync(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
        : panel(cs, dc, rst, busy), canvas(WIDTH, HEIGHT) {}

    // Blocking by design: power-up needs one full refresh (~3 s the first time).
    void begin(const uint8_t sck, const uint8_t mosi, const uint8_t cs) {
        SPI.begin(sck, -1, mosi, cs);   // write-only panel: no MISO
        panel.init(0);
        canvas.fillScreen(PAPER);
        fullRefreshBlocking();
    }

    GFXcanvas1 &gfx() { return canvas; }

    // Cheap and safe to call as often as you like; coalesces while busy.
    void requestRefresh() { pending = true; }

    // Like requestRefresh(), but the next cycle is a full refresh (~1.6 s, flashes).
    // Takes precedence over a pending partial - it shows the latest canvas anyway.
    void requestFullRefresh() {
        pending = true;
        pendingFull = true;
    }

    // Partials completed since the last full refresh (boot's blocking one included).
    uint32_t partialsSinceFull() const { return partialsSinceFullCount; }

    // True while a refresh is in flight (the panel must not be touched).
    bool inFlight() const { return state != State::Idle; }

    // Blocks ~1.6 s. For boot and for clearing ghosting on the firmware's schedule.
    // Uses GxEPD2's own path, which also performs its lazy init and first clear.
    // Must not be called while inFlight().
    void fullRefreshBlocking() {
        memcpy(sent, canvas.getBuffer(), FRAME_BYTES);
        panel.writeImageForFullRefresh(sent, 0, 0, WIDTH, HEIGHT);
        panel.refresh(false);
        panel.writeImageAgain(sent, 0, 0, WIDTH, HEIGHT);
        pending = false;
        pendingFull = false;
        partialsSinceFullCount = 0;
        state = State::Idle;
    }

    // Call every loop(). Returns true on the pass where a refresh completed.
    bool update() {
        switch (state) {
            case State::Idle: {
                if (!pending) {
                    return false;
                }
                if (panel.needsInitialFullRefresh()) {
                    fullRefreshBlocking();   // only if begin() was skipped
                    return true;
                }
                pending = false;
                memcpy(sent, canvas.getBuffer(), FRAME_BYTES);

                if (pendingFull && useBulk()) {
                    pendingFull = false;
                    cycleIsFull = true;

                    const uint32_t t0 = micros();
                    panel.writeRamBulk(0x26, sent);
                    recordStart(micros() - t0);

                    state = State::FullWriteCurrent;
                    return false;
                }

                cycleIsFull = false;
                const uint32_t t0 = micros();
                writeCurrent();
                panel.startFullScreenPartial();
                recordStart(micros() - t0);

                cycleStartMs = millis();
                state = State::WaitBusyHigh;
                return false;
            }

            case State::FullWriteCurrent: {
                const uint32_t t0 = micros();
                panel.writeRamBulk(0x24, sent);
                panel.startFullUpdate();
                recordStart(micros() - t0);

                cycleStartMs = millis();
                state = State::WaitBusyHigh;
                return false;
            }

            case State::WaitBusyHigh:
                // GxEPD2 itself allows 1 ms for BUSY to rise; allow more, but never
                // finish a cycle before seeing it, or 0x26 would be overwritten
                // while the waveform is still running.
                if (panel.busyPinActive()) {
                    state = State::WaitBusyLow;
                } else if (millis() - cycleStartMs > BUSY_RISE_GRACE_MS) {
                    stats.busyNeverRose++;
                    state = State::WaitBusyLow;
                }
                return false;

            case State::WaitBusyLow: {
                if (panel.busyPinActive()) {
                    if (millis() - cycleStartMs < BUSY_TIMEOUT_MS) {
                        return false;
                    }
                    stats.timeouts++;
                }

                const uint32_t t0 = micros();
                writePrevious();
                recordFinish(micros() - t0);

                if (cycleIsFull) {
                    stats.fullRefreshes++;
                    partialsSinceFullCount = 0;
                } else {
                    stats.refreshes++;
                    partialsSinceFullCount++;
                }
                state = State::Idle;
                return true;
            }
        }
        return false;
    }

    const Stats &getStats() const { return stats; }

private:
    enum class State : uint8_t { Idle, FullWriteCurrent, WaitBusyHigh, WaitBusyLow };

    enum : uint32_t { BUSY_RISE_GRACE_MS = 50, BUSY_TIMEOUT_MS = 5000 };

    // Falls back to GxEPD2's own writes until its lazy init and first clear ran.
    bool useBulk() const { return panel.readyForBulkWrites(); }

    void writeCurrent() {
        if (useBulk()) {
            panel.writeRamBulk(0x24, sent);
        } else {
            panel.writeImage(sent, 0, 0, WIDTH, HEIGHT);
        }
    }

    void writePrevious() {
        if (useBulk()) {
            panel.writeRamBulk(0x26, sent);
        } else {
            panel.writeImageToPrevious(sent, 0, 0, WIDTH, HEIGHT);   // 0x26 only
        }
    }

    void recordStart(const uint32_t us) {
        if (us > stats.worstStartUs) {
            stats.worstStartUs = us;
        }
    }

    void recordFinish(const uint32_t us) {
        if (us > stats.worstFinishUs) {
            stats.worstFinishUs = us;
        }
    }

    NonBlockingPanel panel;
    GFXcanvas1 canvas;
    uint8_t sent[FRAME_BYTES];
    bool pending = false;
    bool pendingFull = false;
    bool cycleIsFull = false;
    uint32_t partialsSinceFullCount = 0;
    State state = State::Idle;
    uint32_t cycleStartMs = 0;
    Stats stats;
};

#endif //EINK_ASYNC_H
