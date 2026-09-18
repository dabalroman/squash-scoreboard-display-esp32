"""
Generate the player-setup placard for the V2 e-paper and convert it to a header.

Usage:
    python helpers/player_setup_qr.py

Writes assets/eink/player-setup-qr.png (not in git) and regenerates the committed
src/Display/EInk/Images/PlayerSetupQrImage.h through helpers/eink_image.py.

Both QR targets are fixed - the AP name, its password and the page URL never
change - so nothing is encoded on the device and no QR library ships in the
firmware. Rerun this only if one of the three constants below changes.

Needs Pillow and qrcode:
    python -m pip install pillow qrcode

Everything is drawn in mode "1", so every glyph is thresholded and no grey ever
reaches the PNG - eink_image.py rejects anything that is not pure black and white,
by design, since the panel has no grey either.

The title is PROFILE, matching Str::PLAYER_SETUP_OLED_TITLE - this bitmap cannot
read src/Strings.h, so the two have to be kept in step by hand.

The captions are Polish without diacritics, like every other string the device
shows: POLACZ, OTWORZ, STRONE. The panel itself could render an accent here, since
this is a bitmap rather than a GFX font, but the rest of the UI cannot, and a
placard that spells Polish differently from the screen beside it reads as a bug.
"""

import os
import subprocess
import sys

import qrcode
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')

WIFI_PAYLOAD = 'WIFI:T:WPA;S:Scoreboard;P:19092026;;'
PAGE_URL = 'http://192.168.4.1/'

WIDTH = 128
HEIGHT = 296
MARGIN = 4
LINE = 13        # 10 pt default font, with leading
SCALE = 3        # panel px per QR module
QUIET = 3 * SCALE  # quiet zone kept around each code

# Kept repo-relative: eink_image.py writes the source path into the generated
# header, and that header is committed, so an absolute path would differ per machine.
PNG_PATH = 'assets/eink/player-setup-qr.png'
HEADER_PATH = 'src/Display/EInk/Images/PlayerSetupQrImage.h'

BLACK = 0
WHITE = 1


def fail(message):
    print('error: %s' % message)
    sys.exit(1)


def qr_matrix(payload):
    """Smallest QR that fits, lowest ECC - the placard is read from 20 cm away."""
    code = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_L, box_size=1, border=0)
    code.add_data(payload)
    code.make(fit=True)
    return code.get_matrix()


def draw_qr(image, matrix, top):
    """
    Centred, integer scale only - a fractional one would blur module edges.

    SCALE is fixed rather than maximised: the spec wants a 4-module quiet zone,
    and a code stretched to the full panel width leaves under two. Scanners cope
    with a small code far better than with a large one that touches the edge.
    """
    modules = len(matrix)
    scale = SCALE
    if modules * scale > WIDTH - 2 * QUIET:
        fail('QR of %d modules leaves no quiet zone at scale %d' % (modules, scale))

    size = modules * scale
    if top + size > HEIGHT:
        fail('QR of %d px overflows the panel at y=%d' % (size, top))

    left = (WIDTH - size) // 2

    pixels = image.load()
    for y in range(modules):
        for x in range(modules):
            if not matrix[y][x]:
                continue
            for dy in range(scale):
                for dx in range(scale):
                    pixels[left + x * scale + dx, top + y * scale + dy] = BLACK

    return size


def draw_centered(draw, font, text, top):
    width = draw.textlength(text, font=font)
    if width > WIDTH:
        fail('"%s" is %d px wide, the panel is %d' % (text, width, WIDTH))

    draw.text(((WIDTH - width) / 2, top), text, font=font, fill=BLACK)
    return LINE


def rule(draw, top):
    draw.rectangle([MARGIN, top, WIDTH - MARGIN - 1, top + 1], fill=BLACK)
    return 2


def build():
    # load_default() is a 10 pt FreeTypeFont on Pillow 10, but the canvas is mode
    # "1", so every glyph is thresholded and no grey ever reaches the PNG.
    font = ImageFont.load_default()
    image = Image.new('1', (WIDTH, HEIGHT), WHITE)
    draw = ImageDraw.Draw(image)

    y = 3
    y += draw_centered(draw, font, 'PROFILE', y)
    y += rule(draw, y) + 4

    y += draw_centered(draw, font, '1. POLACZ Z WIFI', y)
    y += QUIET
    y += draw_qr(image, qr_matrix(WIFI_PAYLOAD), y) + QUIET
    y += draw_centered(draw, font, 'Scoreboard', y)
    y += draw_centered(draw, font, '19092026', y)

    y += 2
    y += rule(draw, y) + 4

    y += draw_centered(draw, font, '2. OTWORZ STRONE', y)
    y += QUIET
    y += draw_qr(image, qr_matrix(PAGE_URL), y) + QUIET
    y += draw_centered(draw, font, '192.168.4.1', y)

    if y > HEIGHT:
        fail('placard is %d px tall, the panel is %d' % (y, HEIGHT))

    print('placard uses %d of %d px' % (y, HEIGHT))
    return image


def main():
    image = build()

    os.makedirs(os.path.join(ROOT, os.path.dirname(PNG_PATH)), exist_ok=True)
    image.save(os.path.join(ROOT, PNG_PATH))
    print('%s written' % PNG_PATH)

    result = subprocess.call([sys.executable, os.path.join(HERE, 'eink_image.py'), PNG_PATH, HEADER_PATH], cwd=ROOT)
    if result != 0:
        fail('eink_image.py failed')


if __name__ == '__main__':
    main()
