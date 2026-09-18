#ifndef FIRMWARE_IMAGE_CHECK_H
#define FIRMWARE_IMAGE_CHECK_H

/**
 * Is this .bin an application image for *this* chip?
 *
 * Header fields only - no signature, no MD5, no project sentinel. It is here to
 * catch the wrong attachment (the other board's firmware, a bootloader.bin, a
 * neighbour in the build directory), not an attacker: measured 2026-09-18, the
 * app descriptor says project_name "arduino-lib-builder" and version
 * "esp-idf: v4.4.7" in *every* image this toolchain builds, so nothing in the
 * header identifies this project or its firmware version. An unrelated Arduino
 * build for the same chip therefore passes, and that is the accepted limit.
 *
 * Nothing here is hand-written. Every expected value comes from the same
 * toolchain headers that stamped the running image, so no future build can drift
 * away from what the check expects and lock the board out - and an older .bin
 * built before this feature still passes, because these fields have always been
 * in every image. That is also why the chip id is not a Board.h constant:
 * CONFIG_IDF_FIRMWARE_CHIP_ID comes from the sdkconfig.h that built this
 * firmware (0x0002 on the S2, 0x0009 on the S3) and needs no BOARD_REV branch.
 *
 * Board-agnostic: no #if BOARD_REV.
 */

#include <Arduino.h>
#include <esp_app_format.h>

namespace FirmwareImageCheck {
    // The 24-byte image header, the 8-byte first segment header, and the first word
    // behind them - the app descriptor's magic, which is what separates an application from a bootloader.
    enum : size_t {
        HEADER_BYTES = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(uint32_t)
    };

    static_assert(sizeof(esp_image_header_t) == 24, "ESP image header is not 24 bytes");
    static_assert(sizeof(esp_image_segment_header_t) == 8, "ESP segment header is not 8 bytes");
    static_assert(offsetof(esp_image_header_t, chip_id) == 0x0C, "chip_id is no longer at 0x0C");

    enum class Verdict : uint8_t {
        Pending,        // fewer than HEADER_BYTES bytes seen so far
        Ok,
        NotFirmware,    // no 0xE9, or nothing behind the header that says "application"
        WrongChip,      // an application image, but built for the other board
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

        Verdict verdict() const {
            if (!ready()) {
                return Verdict::Pending;
            }

            if (header[0] != ESP_IMAGE_HEADER_MAGIC) {
                return Verdict::NotFirmware;
            }

            // Wrong board is reported ahead of a missing descriptor: for the one
            // realistic wrong file - the other board's firmware.bin - that is the
            // useful thing to say, and both could otherwise apply.
            if (chipId() != CONFIG_IDF_FIRMWARE_CHIP_ID) {
                return Verdict::WrongChip;
            }

            // bootloader.bin also starts 0xE9 with the right chip id; the app
            // descriptor is the field that tells the two apart.
            if (appDescMagic() != ESP_APP_DESC_MAGIC_WORD) {
                return Verdict::NotFirmware;
            }

            return Verdict::Ok;
        }

        // For the log line only - 0xFFFF while nothing has been read yet.
        uint16_t seenChipId() const {
            return ready() ? chipId() : static_cast<uint16_t>(ESP_CHIP_ID_INVALID);
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
            memcpy(&value, header + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(value));
            return value;
        }
    };

    /**
     * The HTTP 400 body, read by someone who has never seen a build directory: it
     * says what to do, not what the header held. The chip id and the byte count go
     * to the log instead. Not in Strings.h - a browser renders this, so it carries
     * real diacritics, like the rest of the web UI.
     */
    inline const char *reasonFor(const Verdict verdict) {
        switch (verdict) {
            case Verdict::WrongChip:
                return "To nie jest plik dla tej tablicy - sprawdź, czy wysłano właściwy załącznik.";
            case Verdict::NotFirmware:
                return "To nie jest plik z oprogramowaniem tablicy - sprawdź, czy wysłano właściwy załącznik.";
            default:
                return "Nie udało się wgrać pliku.";
        }
    }
}

#endif //FIRMWARE_IMAGE_CHECK_H
