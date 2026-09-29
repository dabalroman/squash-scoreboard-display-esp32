#ifndef FIRMWARE_IMAGE_CHECK_H
#define FIRMWARE_IMAGE_CHECK_H

/**
 * Is this .bin an application image for *this* board?
 *
 * Header fields plus the board marker - no signature, no MD5. It is here to catch
 * the wrong attachment (the other board's firmware, a bootloader.bin, a neighbour
 * in the build directory), not an attacker: the app descriptor says project_name
 * "arduino-lib-builder" and version "esp-idf: v4.4.7" in *every* image this
 * toolchain builds, so nothing there identifies this project.
 *
 * Both boards are an ESP32-S3, so the chip id only rejects images for another
 * chip family. What tells V1 from V2 is BoardMarker (BoardMarker.h), which every
 * build stamps right after esp_app_desc_t. An image without it predates the
 * marker: accepted only where such images exist in the field (V2), because V1's
 * first S3 image went in over USB already marked.
 *
 * The chip id is not a Board.h constant: CONFIG_IDF_FIRMWARE_CHIP_ID comes from
 * the sdkconfig.h that built this firmware, so a build cannot drift from what the
 * check expects.
 *
 * Board-agnostic: no #if BOARD_REV.
 */

#include <cstring>

#include <Arduino.h>
#include <esp_app_format.h>

#include "BoardMarker.h"

namespace FirmwareImageCheck {
    enum : size_t {
        APP_DESC_OFFSET = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
        HEADER_BYTES = BoardMarker::IMAGE_OFFSET + sizeof(BoardMarker::Marker)
    };

    static_assert(sizeof(esp_image_header_t) == 24, "ESP image header is not 24 bytes");
    static_assert(sizeof(esp_image_segment_header_t) == 8, "ESP segment header is not 8 bytes");
    static_assert(sizeof(esp_app_desc_t) == 256, "esp_app_desc_t is not 256 bytes");
    static_assert(offsetof(esp_image_header_t, chip_id) == 0x0C, "chip_id is no longer at 0x0C");
    static_assert(BoardMarker::IMAGE_OFFSET == APP_DESC_OFFSET + sizeof(esp_app_desc_t),
                  "the marker must follow esp_app_desc_t");

    enum class Verdict : uint8_t {
        Pending,        // fewer than HEADER_BYTES bytes seen so far
        Ok,
        NotFirmware,    // no 0xE9, or nothing behind the header that says "application"
        WrongChip,      // an application image for another chip family
        WrongBoard,     // marked for the other board
        MissingMarker,  // unmarked (pre-marker build) on a board that does not accept those
    };

    /**
     * Fed the upload chunk by chunk: a chunk may be shorter than the header, so
     * the first bytes are buffered until there are enough of them to judge.
     */
    class Accumulator {
    public:
        void reset() {
            filled = 0;
        }

        void feed(const uint8_t *data, const size_t len) {
            if (data == nullptr || filled >= HEADER_BYTES) {
                return;
            }

            size_t take = HEADER_BYTES - filled;
            if (take > len) {
                take = len;
            }

            memcpy(header + filled, data, take);
            filled += take;
        }

        bool ready() const {
            return filled >= HEADER_BYTES;
        }

        // receivingRev is a parameter only so the host tests can judge as either board.
        Verdict verdict(const uint8_t receivingRev = BoardMarker::RUNNING_REV) const {
            if (!ready()) {
                return Verdict::Pending;
            }

            if (header[0] != ESP_IMAGE_HEADER_MAGIC) {
                return Verdict::NotFirmware;
            }

            // Wrong chip is reported ahead of a missing descriptor: for a
            // firmware.bin built for another chip that is the useful thing to say.
            if (chipId() != CONFIG_IDF_FIRMWARE_CHIP_ID) {
                return Verdict::WrongChip;
            }

            // bootloader.bin also starts 0xE9 with the right chip id; the app
            // descriptor is the field that tells the two apart.
            if (appDescMagic() != ESP_APP_DESC_MAGIC_WORD) {
                return Verdict::NotFirmware;
            }

            if (!hasMarker()) {
                return receivingRev == BoardMarker::LEGACY_UNMARKED_REV ? Verdict::Ok : Verdict::MissingMarker;
            }

            return marker().boardRev == receivingRev ? Verdict::Ok : Verdict::WrongBoard;
        }

        // For the log line only - 0xFFFF while nothing has been read yet.
        uint16_t seenChipId() const {
            return ready() ? chipId() : static_cast<uint16_t>(ESP_CHIP_ID_INVALID);
        }

        // For the log line only - 0 when unmarked or not yet read.
        uint8_t seenBoardRev() const {
            return ready() && hasMarker() ? marker().boardRev : 0;
        }

    private:
        uint8_t header[HEADER_BYTES] = {};
        size_t filled = 0;

        // memcpy rather than a cast: the buffer carries no alignment, and both the
        // target and the image format are little-endian.
        uint16_t chipId() const {
            uint16_t value = 0;
            memcpy(&value, header + offsetof(esp_image_header_t, chip_id), sizeof(value));
            return value;
        }

        uint32_t appDescMagic() const {
            uint32_t value = 0;
            memcpy(&value, header + APP_DESC_OFFSET, sizeof(value));
            return value;
        }

        BoardMarker::Marker marker() const {
            BoardMarker::Marker value = {};
            memcpy(&value, header + BoardMarker::IMAGE_OFFSET, sizeof(value));
            return value;
        }

        bool hasMarker() const {
            return marker().magic == BoardMarker::MAGIC;
        }
    };

    inline bool isWrongBoard(const Verdict verdict) {
        return verdict == Verdict::WrongChip || verdict == Verdict::WrongBoard || verdict == Verdict::MissingMarker;
    }

    /**
     * The HTTP 400 body, read by someone who has never seen a build directory: it
     * says what to do, not what the header held. The chip id and the byte count go
     * to the log instead. Not in Strings.h - a browser renders this, so it carries
     * real diacritics, like the rest of the web UI.
     */
    inline const char *reasonFor(const Verdict verdict) {
        switch (verdict) {
            case Verdict::WrongChip:
            case Verdict::WrongBoard:
                return "To nie jest plik dla tej tablicy - sprawdź, czy wysłano właściwy załącznik.";
            case Verdict::MissingMarker:
                return "To nie jest plik dla tej tablicy albo to stara wersja - sprawdź, czy wysłano właściwy załącznik.";
            case Verdict::NotFirmware:
                return "To nie jest plik z oprogramowaniem tablicy - sprawdź, czy wysłano właściwy załącznik.";
            default:
                return "Nie udało się wgrać pliku.";
        }
    }
}

#endif //FIRMWARE_IMAGE_CHECK_H
