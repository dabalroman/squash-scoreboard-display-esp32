"""
Dither greyscale artwork to 1-bit for helpers/eink_image.py, which never dithers itself.

Usage:
    python helpers/eink_dither.py assets/eink/splash-text.png [more.png ...]

Writes <name>_dither.png beside each input. Transparency is flattened onto white,
levels are stretched (autocontrast, 1 % cutoff), then Floyd-Steinberg. This is the
exact pipeline behind splash-text_dither.png, so rerunning it reproduces that file
bit for bit. Works for both targets: e-paper (black = ink) and --oled (white = lit)
art is authored as it looks, so the same dither applies.
"""

import os
import sys

from PIL import Image, ImageOps


def dither(source):
    base = Image.open(source).convert('RGBA')
    flat = Image.new('RGBA', base.size, (255, 255, 255, 255))
    flat.alpha_composite(base)
    out = ImageOps.autocontrast(flat.convert('L'), cutoff=1).convert('1')

    stem, _ = os.path.splitext(source)
    target = stem + '_dither.png'
    out.save(target)
    black = sum(1 for v in out.getdata() if v == 0)
    print('%s -> %s  %dx%d, %.1f%% black' % (source, target, out.width, out.height,
                                              100.0 * black / (out.width * out.height)))


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip())
        sys.exit(1)
    for source in sys.argv[1:]:
        dither(source)


if __name__ == '__main__':
    main()
