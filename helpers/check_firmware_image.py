"""
Run the device's own OTA image check against a built .bin, before it is sent.

Usage:
    python helpers/check_firmware_image.py .pio/build/v1/firmware.bin v1
    python helpers/check_firmware_image.py .pio/build/v2/firmware.bin v2

The second argument is the board that will receive the image. The verdict is the
one src/FirmwareImageCheck.h reaches, so a file that passes here is one
POST /update on that board accepts:

    byte 0x000  0xE9        ESP image magic
    byte 0x00C  chip id     0x0009, ESP32-S3 - both boards, so it only rejects other chips
    byte 0x020  0xABCD5432  esp_app_desc magic - an application, not a bootloader
    byte 0x120  0xB0A2D5E7  board marker magic (src/BoardMarker.h), right after esp_app_desc_t
    byte 0x124  board rev   1 = V1, 2 = V2

An unmarked image (built before the marker) passes on v2 only - the legacy V2
case; on v1 it is MissingMarker. Nothing here identifies *this* project: an
unrelated Arduino build for the S3 without the marker passes as a legacy V2 image.
Exits non-zero on any rejection.
"""

import os
import struct
import sys

IMAGE_MAGIC = 0xE9
CHIP_ID_OFFSET = 0x0C
APP_DESC_OFFSET = 0x20      # 24-byte image header + 8-byte segment header
APP_DESC_MAGIC = 0xABCD5432
MARKER_OFFSET = APP_DESC_OFFSET + 256
MARKER_MAGIC = 0xB0A2D5E7
HEADER_BYTES = MARKER_OFFSET + 8

CHIP_ID_S3 = 0x0009
LEGACY_UNMARKED_REV = 2

BOARDS = {'v1': 1, 'v2': 2}

CHIP_NAMES = {
    0x0000: 'ESP32',
    0x0005: 'ESP32-C3',
    0x0009: 'ESP32-S3',
}


def fail(message):
    print('error: %s' % message)
    sys.exit(1)


def verdict(header, receiving_rev):
    """Same order as FirmwareImageCheck::Accumulator::verdict()."""
    if header[0] != IMAGE_MAGIC:
        return 'NotFirmware', 'not an ESP image - byte 0 is 0x%02X, not 0xE9' % header[0]

    chip_id = struct.unpack_from('<H', header, CHIP_ID_OFFSET)[0]
    if chip_id != CHIP_ID_S3:
        return 'WrongChip', 'built for chip 0x%04X, expected 0x%04X (ESP32-S3)' % (chip_id, CHIP_ID_S3)

    if struct.unpack_from('<I', header, APP_DESC_OFFSET)[0] != APP_DESC_MAGIC:
        return 'NotFirmware', ('no application descriptor at 0x%02X - this is not an app image '
                               '(a bootloader.bin looks like this)' % APP_DESC_OFFSET)

    magic, rev = struct.unpack_from('<IB', header, MARKER_OFFSET)
    if magic != MARKER_MAGIC:
        if receiving_rev == LEGACY_UNMARKED_REV:
            return 'Ok', None
        return 'MissingMarker', 'no board marker at 0x%03X - a pre-marker build, v%d rejects it' % (
            MARKER_OFFSET, receiving_rev)

    if rev != receiving_rev:
        return 'WrongBoard', 'marked for board %d, receiving board is v%d' % (rev, receiving_rev)

    return 'Ok', None


def main():
    args = sys.argv[1:]
    if len(args) != 2:
        fail('usage: python helpers/check_firmware_image.py <firmware.bin> v1|v2')

    path, board = args[0], args[1].lower()
    if board not in BOARDS:
        fail('unknown board %r - expected v1 or v2' % args[1])
    receiving_rev = BOARDS[board]

    if not os.path.isfile(path):
        fail('no such file: %s' % path)

    with open(path, 'rb') as handle:
        header = bytearray(handle.read(HEADER_BYTES))

    size = os.path.getsize(path)
    if len(header) < HEADER_BYTES:
        # The device never reaches a verdict and rejects it as NotFirmware at the end.
        fail('%s is only %d bytes - too short to be a firmware image (NotFirmware)' % (path, size))

    chip_id = struct.unpack_from('<H', header, CHIP_ID_OFFSET)[0]
    app_desc = struct.unpack_from('<I', header, APP_DESC_OFFSET)[0]
    marker_magic, marker_rev = struct.unpack_from('<IB', header, MARKER_OFFSET)
    marked = marker_magic == MARKER_MAGIC

    print('file          %s (%d bytes)' % (path, size))
    print('receiving     %s' % board)
    print('image magic   0x%02X  %s' % (header[0], 'ok' if header[0] == IMAGE_MAGIC else 'EXPECTED 0xE9'))
    print('chip id       0x%04X  %s' % (chip_id, CHIP_NAMES.get(chip_id, 'unknown')))
    print('app desc      0x%08X  %s' % (app_desc, 'ok' if app_desc == APP_DESC_MAGIC else 'EXPECTED 0xABCD5432'))
    print('marker        0x%08X  %s' % (marker_magic, 'board %d' % marker_rev if marked else 'absent'))

    result, reason = verdict(header, receiving_rev)
    print('verdict       %s' % result)
    if result != 'Ok':
        fail(reason)

    if marked:
        print('OK - %s would accept this image' % board)
    else:
        print('OK - %s would accept this image (unmarked, legacy V2 build)' % board)


if __name__ == '__main__':
    main()
