#ifndef PLAYER_SETUP_QR_H
#define PLAYER_SETUP_QR_H

/**
 * Board wrapper around the generated placard bitmap, so views can name one
 * symbol without testing BOARD_REV. V1 gets a null pointer and links no
 * PROGMEM at all; EInkDisplay::showImage() returns early on null.
 *
 * Regenerate the image with: python helpers/player_setup_qr.py
 */

#include <Arduino.h>

#if BOARD_REV == 2

#include "PlayerSetupQrImage.h"

constexpr const uint8_t *PLAYER_SETUP_QR_BITMAP = PLAYERSETUPQRIMAGE_BITMAP;

#else

constexpr const uint8_t *PLAYER_SETUP_QR_BITMAP = nullptr;

#endif

#endif //PLAYER_SETUP_QR_H
