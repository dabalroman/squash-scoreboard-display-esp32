#ifndef BOARD_MARKER_H
#define BOARD_MARKER_H

/**
 * Which board an application image was built for, stamped into the image itself.
 *
 * Both boards are an ESP32-S3, so the image header's chip id no longer tells a V1
 * firmware.bin from a V2 one. board_marker lives in .rodata_custom_desc, which the
 * IDF 4.4 linker script (sections.ld, .flash.appdesc) places straight after
 * esp_app_desc_t at the start of the first DROM segment - so in every image it
 * sits at a fixed file offset, IMAGE_OFFSET, where the OTA check reads it.
 *
 * Images built before this marker carry .flash.rodata's first bytes there instead.
 * Only V2 ever ran such an S3 image over OTA (V1's first S3 image went in over
 * USB), so LEGACY_UNMARKED_REV is the one board that still accepts them.
 *
 * Header-only apart from board_marker's definition (BoardMarker.cpp), so host tests
 * include it without linking the .cpp. BOARD_REV is used as a value, not an #if.
 */

#include <cstddef>
#include <cstdint>

namespace BoardMarker {
    // No printable ASCII byte, so rodata strings in a legacy image cannot fake it.
    enum : uint32_t { MAGIC = 0xB0A2D5E7u };

    // 24-byte image header + 8-byte first segment header + 256-byte esp_app_desc_t.
    enum : size_t { IMAGE_OFFSET = 24 + 8 + 256 };

    enum : uint8_t {
        RUNNING_REV = BOARD_REV,
        LEGACY_UNMARKED_REV = 2,
    };

    struct Marker {
        uint32_t magic;
        uint8_t boardRev;
        uint8_t reserved[3];
    };

    static_assert(sizeof(Marker) == 8, "Marker layout is part of the image format");
    static_assert(offsetof(Marker, boardRev) == 4, "boardRev is no longer at +4");
}

// This image's own marker. extern "C" keeps the symbol unmangled, for a
// `-Wl,-u,board_marker` should a reference from code ever stop being enough.
extern "C" const BoardMarker::Marker board_marker;

#endif //BOARD_MARKER_H
