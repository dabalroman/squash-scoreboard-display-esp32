#ifndef LED_BAJGIEL_ANIMATION_H
#define LED_BAJGIEL_ANIMATION_H

#include <Arduino.h>
#include <FastLED.h>
#include <math.h>

#include "Board.h"
#include "Display/LedDisplay/Layers/LedAnimation.h"
#include "LedSlotPositions.h"

/**
 * The bajgiel (a game won to zero): a comet circling each of the loser's two 0
 * glyphs, both in phase. A grey level for Multiply - 255 at the head, fading
 * along the tail, 0 on the rest of the ring - so it dims the base 0 (already in
 * the loser's colour) and needs no colour of its own. Under LitOnly the 0's
 * unlit centre is never touched.
 *
 * Each slot's angle is taken around its own digit's bounding-box centre, once
 * in start() from LedSlotPositions + the caller's elementMap bits - never a
 * hand-kept slot order. Render is integer-only (the S2 has no FPU).
 *
 * renderFrame() stays clock-free so the host checks can step it.
 */

class LedBajgielAnimation : public LedAnimation {
public:
    struct Params {
        uint16_t durationMs;
        uint16_t periodMs;   ///< one revolution
        float tail;          ///< fraction of a revolution behind the head
    };

    static Params defaults() {
        return Params{5000, 1000, 0.5f};
    }

private:
    Params params;
    uint32_t tailTurns = 0;   ///< tail in 1/65536 turns
    uint32_t startedMs = 0;
    bool running = false;

    // Set by start(): 1/65536 turns, clockwise on the panel from +x.
    uint16_t angle[Board::LED_COUNT] = {};
    bool member[Board::LED_COUNT] = {};

    void addDigit(const uint16_t *elementMap, const uint16_t bit) {
        int16_t minX = 0, maxX = 0, minY = 0, maxY = 0;
        bool any = false;
        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            if (!(elementMap[slot] & bit) || LedSlots::POS[slot][0] == LedSlots::SKIP) continue;
            const int16_t x = LedSlots::POS[slot][0];
            const int16_t y = LedSlots::POS[slot][1];
            if (!any || x < minX) minX = x;
            if (!any || x > maxX) maxX = x;
            if (!any || y < minY) minY = y;
            if (!any || y > maxY) maxY = y;
            any = true;
        }
        if (!any) return;

        const float cx = (static_cast<float>(minX) + static_cast<float>(maxX)) * 0.5f;
        const float cy = (static_cast<float>(minY) + static_cast<float>(maxY)) * 0.5f;
        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            if (!(elementMap[slot] & bit) || LedSlots::POS[slot][0] == LedSlots::SKIP) continue;
            // +y is down, so atan2 grows clockwise as seen on the panel.
            float turns = atan2f(static_cast<float>(LedSlots::POS[slot][1]) - cy,
                                 static_cast<float>(LedSlots::POS[slot][0]) - cx) / 6.2831853f;
            if (turns < 0.0f) turns += 1.0f;
            angle[slot] = static_cast<uint16_t>(static_cast<uint32_t>(turns * 65536.0f) & 0xFFFF);
            member[slot] = true;
        }
    }

public:
    explicit LedBajgielAnimation(const Params params = defaults()) : params(params) {
    }

    void start(const uint32_t nowMs, const uint16_t *elementMap, const uint16_t digitBitA, const uint16_t digitBitB) {
        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) member[slot] = false;
        addDigit(elementMap, digitBitA);
        addDigit(elementMap, digitBitB);

        tailTurns = static_cast<uint32_t>(params.tail * 65536.0f);
        if (tailTurns == 0) tailTurns = 1;
        startedMs = nowMs;
        running = true;
    }

    void stop() {
        running = false;
    }

    uint32_t durationMs() const {
        return params.durationMs;
    }

    bool active(const uint32_t nowMs) const override {
        return running && (nowMs - startedMs) < params.durationMs;
    }

    void render(const uint32_t nowMs, CRGB *out) const override {
        if (!active(nowMs)) {
            return;
        }

        renderFrame(nowMs - startedMs, out);
    }

    /** Head angle at `elapsedMs`, 1/65536 turns - for the host checks. */
    uint16_t headAt(const uint32_t elapsedMs) const {
        return static_cast<uint16_t>(((elapsedMs % params.periodMs) << 16) / params.periodMs);
    }

    uint16_t angleOf(const uint16_t slot) const {
        return angle[slot];
    }

    bool isMember(const uint16_t slot) const {
        return member[slot];
    }

    /** Clock-free, so the host checks can step it frame by frame. */
    void renderFrame(const uint32_t elapsedMs, CRGB *out) const {
        const uint16_t head = headAt(elapsedMs);

        for (uint16_t slot = 0; slot < Board::LED_COUNT; slot++) {
            if (!member[slot]) continue;

            const uint32_t behind = static_cast<uint16_t>(head - angle[slot]);
            const uint8_t level = behind < tailTurns
                ? static_cast<uint8_t>(255 - behind * 255 / tailTurns)
                : 0;
            out[slot] = CRGB(level, level, level);
        }
    }
};

#endif //LED_BAJGIEL_ANIMATION_H
