"""Renders the shared glyph table as ASCII art and verifies the 7-segment collapse.

Parses src/Display/LedDisplay/GlyphMasks.h directly, so it cannot drift from the
firmware. Run after editing the table:

    python helpers/preview_glyphs.py            # preview + checks
    python helpers/preview_glyphs.py check      # checks only

Checks:
  * enum and mask table are index-aligned;
  * `mask & 0x7F` equals the original V1 7-segment table (indices 0..36), taken
    frozen below as V1_MASKS (Glyph::All differs only in unread bit 7).
Ported from the 9-segment rig (squash-scoreboard-display-testing-playground).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MASKS = os.path.join(HERE, '..', 'src', 'Display', 'LedDisplay', 'GlyphMasks.h')
# Frozen V1 7-segment table (SegmentToGlyphMap, indices 0..36), copied from the
# pre-V2 LedGlyph.h that helpers/led_dump/v1_snapshot used to hold.
V1_MASKS = [
    0b01110111, 0b00100100, 0b01101011, 0b01101101, 0b00111100, 0b01011101, 0b01011111, 0b01100100,
    0b01111111, 0b01111101, 0b01111110, 0b01010011, 0b01011010, 0b01010111, 0b00010011, 0b01111010,
    0b00110111, 0b00111101, 0b00001000, 0b00001111, 0b01111000, 0b11111111, 0b00000000, 0b00011111,
    0b01011011, 0b00111110, 0b00001110, 0b00001111, 0b00001010, 0b00011011, 0b00000111, 0b00001011,
    0b00011110, 0b01011101, 0b01101011, 0b00100100, 0b00101111,
]

BOT, BL, BR, CEN, UL, TR, TOP, ML, MR = (1 << i for i in range(9))


def art(mask):
    on = lambda seg: '#' if mask & seg else '.'
    return [
        ' ' + on(TOP) * 3 + ' ',
        on(UL) + '   ' + on(TR),
        on(ML) + ' ' + on(CEN) + ' ' + on(MR),
        on(BL) + '   ' + on(BR),
        ' ' + on(BOT) * 3 + ' ',
    ]


def grid(pairs, per_row=8, width=10):
    for start in range(0, len(pairs), per_row):
        chunk = pairs[start:start + per_row]
        print('  '.join(name.center(width) for name, _ in chunk))
        blocks = [art(mask) for _, mask in chunk]
        for line in range(5):
            print('  '.join(block[line].center(width) for block in blocks))
        print()


def load():
    src = open(MASKS, encoding='utf-8').read()
    enum_body = src.split('enum class Glyph : uint8_t {')[1].split('};')[0]
    names = [n for n in re.findall(r'^\s*(\w+)\s*=\s*\d+', enum_body, re.M)]
    table = src.split('constexpr uint16_t GlyphMasks[GLYPH_COUNT] = {')[1].split('};')[0]
    masks = [int(m, 2) for m in re.findall(r'0b([01]{9})', table)]
    return names, masks


def check(names, masks):
    ok = True
    if len(names) != len(masks):
        print('FAIL: enum has %d entries, mask table %d' % (len(names), len(masks)))
        ok = False

    v1_masks = V1_MASKS
    for index, old in enumerate(v1_masks):
        collapsed = masks[index] & 0x7F
        if collapsed != old & 0x7F:
            print('FAIL: %s collapses to %s, V1 had %s' % (names[index], format(collapsed, '07b'), format(old, '08b')))
            ok = False
    print('%s: %d glyphs, 7-segment collapse matches V1 for %d' % ('OK' if ok else 'FAIL', len(masks), len(v1_masks)))
    return ok


def main():
    names, masks = load()
    if not (len(sys.argv) > 1 and sys.argv[1] == 'check'):
        pairs = list(zip(names, masks))
        print('9-SEGMENT (V2)')
        grid(pairs)
        print('7-SEGMENT COLLAPSE (V1, mask & 0x7F)')
        grid([(n, m & 0x7F) for n, m in pairs])
    sys.exit(0 if check(names, masks) else 1)


if __name__ == '__main__':
    main()
