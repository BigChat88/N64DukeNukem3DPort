#!/usr/bin/env python3
"""Extracts files from a Build engine GRP archive.

Usage: grp_extract.py <file.grp> <pattern> <output dir>

<pattern> is a shell-style pattern matched case-insensitively against the
names stored in the archive (e.g. '*.mid'). Files are written in lowercase.

GRP layout (Ken Silverman): "KenSilverman" (12 bytes), file count (uint32 LE),
then a 16-byte entry per file (12-byte name + uint32 LE size), then the data
of every file one after another.
"""
import fnmatch
import os
import struct
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    grp_path, pattern, out_dir = sys.argv[1:]

    with open(grp_path, 'rb') as grp:
        header = grp.read(16)
        if header[:12] != b'KenSilverman':
            sys.exit(f'{grp_path}: not a GRP archive')
        (count,) = struct.unpack('<I', header[12:])
        index = grp.read(16 * count)

        os.makedirs(out_dir, exist_ok=True)
        offset = 16 + 16 * count
        extracted = 0
        for i in range(count):
            entry = index[i * 16:(i + 1) * 16]
            name = entry[:12].split(b'\0')[0].decode('ascii', 'replace').lower()
            (size,) = struct.unpack('<I', entry[12:])
            if fnmatch.fnmatch(name, pattern.lower()):
                grp.seek(offset)
                with open(os.path.join(out_dir, name), 'wb') as out:
                    out.write(grp.read(size))
                extracted += 1
            offset += size

    print(f'{grp_path}: {extracted} file(s) matching {pattern} extracted to {out_dir}')


if __name__ == '__main__':
    main()
