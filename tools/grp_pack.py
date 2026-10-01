#!/usr/bin/env python3
"""Packs a Duke Nukem 3D add-on (expansion pack, user maps) into one GRP.

Usage: grp_pack.py <add-on> <output.grp>

<add-on> is one of:
  - a GRP archive (e.g. DUKEDC.GRP): copied as is;
  - a Sunstorm add-on installer archive (.SSI, e.g. DUKEDCPP.SSI);
  - a folder of loose files (maps, CON scripts, ART, sounds, music), as the
    CD releases of the expansions install them;
  - a .zip archive holding either of the above.

GRP and SSI archives found in a folder or zip are unpacked first; loose files are
added after them and replace files of the same name, as the game would when
loading them from disk. Files that the game never loads (programs,
documents, setup files) and names longer than the 12 characters of a GRP
entry are left out.

GRP layout (Ken Silverman): "KenSilverman" (12 bytes), file count (uint32 LE),
then a 16-byte entry per file (12-byte name + uint32 LE size), then the data
of every file one after another.
"""
import os
import shutil
import struct
import sys
import zipfile

SKIP_EXTENSIONS = {
    '.exe', '.com', '.bat', '.dll', '.pif', '.ico', '.ini', '.cfg', '.txt',
    '.doc', '.diz', '.nfo', '.htm', '.html', '.pdf', '.dat', '.pck', '.lnk',
}


def read_grp(data):
    if data[:12] != b'KenSilverman':
        raise ValueError('not a GRP archive')
    (count,) = struct.unpack('<I', data[12:16])
    offset = 16 + 16 * count
    files = {}
    for i in range(count):
        entry = data[16 + i * 16:32 + i * 16]
        name = entry[:12].split(b'\0')[0].decode('ascii', 'replace').upper()
        (size,) = struct.unpack('<I', entry[12:])
        files[name] = data[offset:offset + size]
        offset += size
    return files


def read_ssi(data):
    """Sunstorm Interactive's add-on archives (GAMER installer): a header
    (version, file count, title, optional program name, 3 description
    lines), then 121 bytes per file (12-byte Pascal name, uint32 LE size,
    installer data), then the data of every file one after another."""
    version, count = struct.unpack('<ii', data[:8])
    if version not in (1, 2) or not 0 < count < 4096:
        raise ValueError('not an SSI archive')
    pos = 8 + 33 + (13 if version == 2 else 0) + 3 * 71
    entries = []
    for _ in range(count):
        name = data[pos + 1:pos + 1 + min(data[pos], 12)].decode('ascii', 'replace').upper()
        (size,) = struct.unpack('<I', data[pos + 13:pos + 17])
        entries.append((name, size))
        pos += 13 + 4 + 104
    files = {}
    for name, size in entries:
        files[name] = data[pos:pos + size]
        pos += size
    if pos != len(data):
        raise ValueError('not an SSI archive (sizes do not add up)')
    return files


def write_grp(path, files):
    with open(path, 'wb') as out:
        out.write(b'KenSilverman' + struct.pack('<I', len(files)))
        for name, data in files.items():
            out.write(name.encode('ascii').ljust(12, b'\0') + struct.pack('<I', len(data)))
        for data in files.values():
            out.write(data)


def collect(entries):
    """entries: (name, read function) pairs of a folder or zip."""
    grps, loose, skipped = [], [], []
    for name, read in entries:
        base = os.path.basename(name)
        ext = os.path.splitext(base)[1].lower()
        if ext in ('.grp', '.ssi'):
            grps.append((base, read))
        elif ext in SKIP_EXTENSIONS or not base:
            continue
        elif len(base) > 12:
            skipped.append(base)
        else:
            loose.append((base.upper(), read))

    files = {}
    for base, read in sorted(grps):
        print(f'  {base}: unpacked')
        data = read()
        files.update(read_ssi(data) if base.lower().endswith('.ssi') else read_grp(data))
    for base, read in loose:
        files[base] = read()
    if skipped:
        print('  left out (name longer than 12 characters): ' + ', '.join(skipped))
    return files


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1:]

    if os.path.isfile(src) and src.lower().endswith('.grp'):
        with open(src, 'rb') as f:
            if f.read(12) != b'KenSilverman':
                sys.exit(f'{src}: not a GRP archive')
        shutil.copyfile(src, dst)
        print(f'{src}: copied to {dst}')
        return

    if os.path.isfile(src) and src.lower().endswith('.ssi'):
        entries = [(os.path.basename(src), lambda: open(src, 'rb').read())]
    elif os.path.isdir(src):
        entries = []
        for root, _, names in os.walk(src):
            for n in names:
                p = os.path.join(root, n)
                entries.append((n, lambda p=p: open(p, 'rb').read()))
    elif zipfile.is_zipfile(src):
        z = zipfile.ZipFile(src)
        entries = [(i.filename, lambda i=i: z.read(i)) for i in z.infolist() if not i.is_dir()]
    else:
        sys.exit(f'{src}: expected a GRP, a folder or a zip')

    files = collect(entries)
    if not files:
        sys.exit(f'{src}: no game files found')
    write_grp(dst, files)
    cons = sorted(n for n in files if n.endswith('.CON'))
    print(f'{src}: {len(files)} files packed into {dst} (CON: {", ".join(cons) or "none"})')


if __name__ == '__main__':
    main()
