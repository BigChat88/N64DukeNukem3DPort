#!/usr/bin/env python3
"""Extracts the files of a data CD image (.bin/.iso, ISO 9660).

Usage: cd_extract.py <image> <output dir>

Handles plain 2048-byte-sector images (.iso) and raw 2352-byte-sector
images (.bin of a .cue, MODE1 or MODE2 form 1), as the expansion packs of
Duke Nukem 3D are sometimes distributed. File names lose their ";1"
version suffix; folders are kept.
"""
import os
import struct
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    image, out_dir = sys.argv[1:]
    data = open(image, 'rb').read()

    # Find the primary volume descriptor (sector 16) to tell the layout.
    for sector_size, header in ((2048, 0), (2352, 16), (2352, 24)):
        off = 16 * sector_size + header
        if data[off:off + 6] == b'\x01CD001':
            break
    else:
        sys.exit(f'{image}: no ISO 9660 file system found')

    def read(lba, length):
        out = bytearray()
        while length > 0:
            start = lba * sector_size + header
            chunk = data[start:start + min(2048, length)]
            out += chunk
            length -= len(chunk)
            lba += 1
            if not chunk:
                break
        return bytes(out)

    pvd = read(16, 2048)
    root = pvd[156:156 + 34]

    count = 0

    def walk(record, path):
        nonlocal count
        lba, size = struct.unpack('<I', record[2:6])[0], struct.unpack('<I', record[10:14])[0]
        body = read(lba, size)
        pos = 0
        while pos < len(body):
            length = body[pos]
            if length == 0:     # records do not cross sectors: next sector
                pos = (pos // 2048 + 1) * 2048
                continue
            rec = body[pos:pos + length]
            name_len = rec[32]
            name = rec[33:33 + name_len]
            pos += length
            if name in (b'\x00', b'\x01'):
                continue
            name = name.decode('ascii', 'replace').split(';')[0].rstrip('.')
            if rec[25] & 2:
                walk(rec, os.path.join(path, name))
            else:
                flba, fsize = struct.unpack('<I', rec[2:6])[0], struct.unpack('<I', rec[10:14])[0]
                os.makedirs(path, exist_ok=True)
                with open(os.path.join(path, name), 'wb') as f:
                    f.write(read(flba, fsize))
                count += 1

    walk(root, out_dir)
    print(f'{image}: {count} files extracted to {out_dir}')


if __name__ == '__main__':
    main()
