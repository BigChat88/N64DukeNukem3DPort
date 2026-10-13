#!/usr/bin/env python3
"""
shots.py -- PNG files from the screenshots of a SHOTS=1 build's log.

    libdragon make BENCH=20 SHOTS=1     # one screenshot per level, 6 s in
    python tools/shots.py run.log out_dir/

The log holds "shot: E1L1 320 200" and then "S y x hex..." lines of RGBA5551
pixels (see shot_dump in src/n64/n64_display.c). Needs Pillow.
"""
import re
import sys
from pathlib import Path

from PIL import Image

RE_SHOT = re.compile(r"shot: (\S+) (\d+) (\d+)")
RE_ROW = re.compile(r"\bS (\d+) (\d+) ([0-9a-f]+)")


def rgb(p: int):
    return ((p >> 11) & 31) * 255 // 31, ((p >> 6) & 31) * 255 // 31, ((p >> 1) & 31) * 255 // 31


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit("usage: shots.py LOG OUT_DIR")
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    img = None
    label = None

    def save():
        if img is not None:
            path = out / f"{label}.png"
            img.resize((img.width * 2, img.height * 2 * 6 // 5), Image.NEAREST).save(path)   # 4:3 like a TV
            print(path)

    with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
        for line in f:
            m = RE_SHOT.search(line)
            if m:
                save()
                label = m.group(1)
                img = Image.new("RGB", (int(m.group(2)), int(m.group(3))))
                continue
            m = RE_ROW.search(line)
            if m and img is not None:
                y, x, hexs = int(m.group(1)), int(m.group(2)), m.group(3)
                for k in range(len(hexs) // 4):
                    if x + k < img.width and y < img.height:
                        img.putpixel((x + k, y), rgb(int(hexs[k*4:k*4+4], 16)))
    save()


if __name__ == "__main__":
    main()
