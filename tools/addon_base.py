#!/usr/bin/env python3
"""Trims the base game GRP down to what an add-on ROM uses.

Usage: addon_base.py <base DUKE3D.GRP> <add-on GRP> <output GRP>

An add-on ROM is its own game: it plays only the episodes the add-on has maps
for. From the base GRP this leaves out:
  - the files the add-on replaces (same name: the game would load the
    add-on's anyway), e.g. its own ART files;
  - the maps of the other episodes;
  - the cutscenes (ANM) of the other episodes;
  - the demos (not played on the N64);
  - the base CON scripts when the add-on brings its own main script.

The episodes are read from the add-on's CON scripts (definelevelname), or
from the base USER.CON if it defines none; those kept are the ones most of
whose maps are the add-on's. Everything else (art, sounds,
music) stays: the scripts may use any of it.

The size of the original GRP is stored in the entry GRPSIZE.N64, so that the
game still recognizes the base version (see initgroupfile, filesystem.c), and
the episodes kept in EPISODES.N64 (see n64_addon_volumes, menues.c).
"""
import re
import struct
import sys

# Cutscenes played at the start or the end of each episode (0-based), see
# dobonus (game.c) and newgame (premap.c). LOGO.ANM is the intro.
EPISODE_ANMS = {
    1: {'CINEOV2.ANM'},
    2: {'CINEOV3.ANM', 'RADLOGO.ANM'},
    3: {'VOL41A.ANM', 'VOL42A.ANM', 'VOL43A.ANM', 'VOL4E1.ANM', 'VOL4E2.ANM',
        'VOL4E3.ANM', 'DUKETEAM.ANM'},
}
BASE_CONS = {'GAME.CON', 'DEFS.CON', 'USER.CON'}


def read_grp(path):
    data = open(path, 'rb').read()
    if data[:12] != b'KenSilverman':
        sys.exit(f'{path}: not a GRP archive')
    (count,) = struct.unpack('<I', data[12:16])
    offset = 16 + 16 * count
    files = {}
    for i in range(count):
        entry = data[16 + i * 16:32 + i * 16]
        name = entry[:12].split(b'\0')[0].decode('ascii', 'replace').upper()
        (size,) = struct.unpack('<I', entry[12:])
        files[name] = data[offset:offset + size]
        offset += size
    return files, len(data)


def write_grp(path, files):
    with open(path, 'wb') as out:
        out.write(b'KenSilverman' + struct.pack('<I', len(files)))
        for name, data in files.items():
            out.write(name.encode('ascii').ljust(12, b'\0') + struct.pack('<I', len(data)))
        for data in files.values():
            out.write(data)


def levels(con_texts):
    """{volume: set of map files} from definelevelname commands."""
    vols = {}
    for text in con_texts:
        text = re.sub(r'//[^\n]*', '', text)
        for v, _, f in re.findall(r'(?i)definelevelname\s+(\d+)\s+(\d+)\s+(\S+)', text):
            vols.setdefault(int(v), set()).add(f.upper())
    return vols


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    base_path, addon_path, out_path = sys.argv[1:]
    base, base_size = read_grp(base_path)
    addon, _ = read_grp(addon_path)

    addon_cons = [d.decode('latin-1') for n, d in addon.items() if n.endswith('.CON')]
    vols = levels(addon_cons) or levels([base[n].decode('latin-1') for n in ('USER.CON', 'GAME.CON') if n in base])
    # Its episodes: most of their maps are the add-on's (Caribbean also has a
    # Dukematch episode of 4 new arenas among base maps: left out).
    kept = sorted(v for v, maps in vols.items() if len(maps & set(addon)) * 2 > len(maps))
    if not kept:
        kept = sorted(v for v, maps in vols.items() if maps & set(addon))
    if not kept:
        kept = sorted(vols)     # no maps of its own: keep every episode
    maps = set().union(*(vols[v] for v in kept)) if kept else set()
    anms = {'LOGO.ANM'}.union(*(EPISODE_ANMS.get(v, set()) for v in kept))

    includes = set()
    for text in addon_cons:
        includes.update(f.upper() for f in re.findall(r'(?i)^\s*include\s+(\S+)', text, re.M))
    own_main_con = any(n.endswith('.CON') and n not in BASE_CONS for n in addon)

    out, dropped = {}, 0
    for name, data in base.items():
        drop = (name in addon or
                (name.endswith('.MAP') and name not in maps) or
                (name.endswith('.ANM') and name not in anms) or
                name.endswith('.DMO') or
                (own_main_con and name in BASE_CONS and name not in includes))
        if drop:
            dropped += len(data)
        else:
            out[name] = data
    out['GRPSIZE.N64'] = str(base_size).encode('ascii')
    # The episodes the ROM offers (0-based), for its episode menu: the others
    # may still have a first map around (Caribbean's Dukematch arenas).
    out['EPISODES.N64'] = ' '.join(str(v) for v in kept).encode('ascii')
    write_grp(out_path, out)

    print(f'{base_path}: episodes {", ".join(str(v + 1) for v in kept)} kept, '
          f'{dropped / 1e6:.1f} MB left out -> {out_path}')


if __name__ == '__main__':
    main()
