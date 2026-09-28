#ifndef BAJGIEL_H
#define BAJGIEL_H

/**
 * Board wrapper around the generated bajgiel picture (a game won to zero), so
 * views can name one symbol without testing BOARD_REV. V1 gets a null pointer
 * and links no e-paper PROGMEM; EInkDisplay::showImage() returns early on null.
 *
 * Regenerate with: python helpers/eink_image.py assets/eink/bajgiel-eink.png src/Display/EInk/Images/BajgielImage.h
 */

#include <Arduino.h>

#if BOARD_REV == 2

#include "BajgielImage.h"

constexpr const uint8_t *BAJGIEL_EINK_BITMAP = BAJGIELIMAGE_BITMAP;

#else

constexpr const uint8_t *BAJGIEL_EINK_BITMAP = nullptr;

#endif

#endif //BAJGIEL_H
