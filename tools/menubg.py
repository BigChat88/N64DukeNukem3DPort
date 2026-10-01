#!/usr/bin/env python3
"""Makes the main menu background of an add-on ROM from a picture.

Usage: menubg.py <picture> <output.png>

The picture (any size, 4:3 like a TV) is scaled to 320x200, the size of the
game's screen that the N64 video output stretches to 4:3, and reduced to 256
colors with dithering: the build turns the PNG into a CI8 sprite
(64 KB in RAM instead of 128 KB as 16-bit colour).

Needs Pillow (pip install pillow); run it on the host, the PNG goes into
assets/addons/<add-on name>.png (see the Makefile).
"""
import sys

from PIL import Image


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1:]
    img = Image.open(src).convert('RGB').resize((320, 200), Image.LANCZOS)
    img = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    img.save(dst, optimize=True)
    print(f'{src} -> {dst}')


if __name__ == '__main__':
    main()
