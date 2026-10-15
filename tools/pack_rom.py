#!/usr/bin/env python3
"""
pack_rom.py -- turn your own copy of Duke Nukem 3D (and its expansions) into
Nintendo 64 ROMs, without Docker or the N64 toolchain.

It does everything the ROM build does *except* compile the game. The compiled
game (duke3d.elf.stripped + .sym) and the project's media, already converted
(soundfont, boot logo, menu backgrounds), are the same for everyone: grab
them from a GitHub Release (they ship in tools/vendor/engine/), or build them
with `make engine` inside `libdragon exec` (they land in build/prebuilt/,
which is also searched).

    python tools/pack_rom.py                  # every ROM gamedata/ allows
    python tools/pack_rom.py base             # only the base game
    python tools/pack_rom.py nwinter          # only gamedata/nwinter/
    python tools/pack_rom.py path/to/MYMAPS.zip --con MYMAPS.CON

Game data (see gamedata/README.md):

    gamedata/DUKE3D.GRP          base game: version 1.4 or 1.5
    gamedata/DUKE.RTS            optional (Remote Ridicule)
    gamedata/<name>/             one folder per expansion -> duke3d-<name>.z64

Instead of DUKE3D.GRP, gamedata/ may hold a .zip with it inside. An expansion
folder holds its GRP, its Sunstorm .SSI installer, its loose files, or a
single .zip of any of them, or its CD image (.bin/.cue or .iso, also zipped). Folders whose name starts with '_' are skipped.
Only version 1.4 (Plutonium PAK) or 1.5 (Atomic Edition) is supported, for the base game
and as the base of the expansions.

Requires the libdragon host tools (mkdfs, n64tool, ed64romconfig and
audioconv64 for the music) -- found via --tools-dir, $N64_INST/bin,
tools/vendor/bin/ or PATH. See tools/vendor/bin/README.md.
"""
from __future__ import annotations

import argparse
import fnmatch
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

ROM_NAME = "duke3d"
BASE_GRP = "DUKE3D.GRP"
RTS_FILE = "DUKE.RTS"
ENGINE_FILES = (f"{ROM_NAME}.elf.stripped", f"{ROM_NAME}.elf.sym")
SOUNDFONT_NAME = "soundfont.sf64"
# The soundfont the engine ships converted; converted here only if missing or
# replaced with --soundfont.
DEFAULT_SOUNDFONT = ROOT / "assets" / "soundfont" / "SC-55-SoundFont-v1.2a1.sf2"
# Fallback header settings if the engine folder has no rom.cfg (written by
# `make engine`, which takes them from the Makefile).
DEFAULT_ROM_CFG = {"title": "Duke Nukem 3D", "savetype": "flashram",
                   "expansionpak": "required", "regionfree": "1"}


def project_version() -> str:
    try:
        return (ROOT / "VERSION").read_text(encoding="utf-8").strip() or "0.0.0"
    except OSError:
        return "0.0.0"


def log(msg: str) -> None:
    print(msg, flush=True)


def die(msg: str):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def run(cmd: list, **kw) -> None:
    log("  $ " + " ".join(str(c) for c in cmd))
    try:
        subprocess.run([str(c) for c in cmd], check=True, **kw)
    except subprocess.CalledProcessError:
        die(f"{Path(str(cmd[1] if str(cmd[0]) == sys.executable else cmd[0])).name} failed: see its message above")


def run_tool(script: str, *args) -> None:
    """One of the GRP tools next to this script (grp_pack.py, addon_base.py, ...)."""
    run([sys.executable, HERE / script, *args])


def exe(name: str) -> str:
    return name + (".exe" if os.name == "nt" else "")


def find_tool(name: str, extra_dir: Path | None) -> Path:
    cands: list = []
    if extra_dir:
        cands.append(extra_dir / exe(name))
    n64_inst = os.environ.get("N64_INST")
    if n64_inst:
        cands.append(Path(n64_inst) / "bin" / exe(name))
    cands.append(HERE / "vendor" / "bin" / exe(name))
    which = shutil.which(name)
    if which:
        cands.append(Path(which))
    for c in cands:
        if c.is_file():
            return c
    die(
        f"host tool '{name}' not found.\n"
        f"  looked in: {', '.join(str(c) for c in cands)}\n"
        f"  download a Release, build it with tools/vendor/bin/build-tools.sh,\n"
        f"  install the libdragon toolchain and set N64_INST, or pass --tools-dir"
    )


def find_engine(engine_dir: Path | None) -> Path:
    dirs = [engine_dir] if engine_dir else [HERE / "vendor" / "engine", ROOT / "build" / "prebuilt"]
    for d in dirs:
        if all((d / f).is_file() for f in ENGINE_FILES):
            return d
    die(
        f"compiled game not found ({' + '.join(ENGINE_FILES)}).\n"
        f"  looked in: {', '.join(str(d) for d in dirs)}\n"
        "  download a Release, build it with `make engine` inside\n"
        "  `libdragon exec`, or pass --engine-dir. See tools/vendor/engine/README.md"
    )


def read_rom_cfg(engine: Path) -> dict:
    cfg = dict(DEFAULT_ROM_CFG)
    f = engine / "rom.cfg"
    if f.is_file():
        for line in f.read_text(encoding="utf-8").splitlines():
            key, sep, value = line.partition("=")
            if sep:
                cfg[key.strip()] = value.strip()
    return cfg


# ---------------------------------------------------------------------------
# Game data
# ---------------------------------------------------------------------------
def grp_names(data: bytes) -> list:
    if data[:12] != b"KenSilverman":
        return []
    (count,) = struct.unpack("<I", data[12:16])
    return [data[16 + i * 16:28 + i * 16].split(b"\0")[0].decode("ascii", "replace").upper()
            for i in range(count)]


def find_in_zip(z: zipfile.ZipFile, name: str) -> str | None:
    """The shallowest entry of the zip called name (any letter case)."""
    hits = [n for n in z.namelist() if n.rsplit("/", 1)[-1].upper() == name]
    return min(hits, key=lambda n: n.count("/")) if hits else None


def find_file(folder: Path, name: str) -> Path | None:
    for p in folder.iterdir() if folder.is_dir() else ():
        if p.is_file() and p.name.upper() == name:
            return p
    return None


def check_atomic(grp: Path) -> None:
    """Only versions 1.4 and 1.5 are supported, the ones the expansions are
    made for: their scripts use what its DEFS.CON adds, and over an older base
    game they stopped at boot ("Parameter ... is undefined"). Only they have
    episode 4."""
    if "E4L1.MAP" not in grp_names(grp.read_bytes()):
        die(f"{BASE_GRP} (from gamedata/) is an older version of Duke Nukem 3D (1.3D or shareware:\n"
            "  it has no episode 4). Only version 1.4 (Plutonium PAK) or 1.5 (Atomic Edition)\n"
            "  is supported: copy its DUKE3D.GRP into gamedata/. See gamedata/README.md")


def stage_base(gamedata: Path, dest: Path) -> None:
    """Copies DUKE3D.GRP (and DUKE.RTS) into dest, from gamedata/ or a zip in it."""
    grp = find_file(gamedata, BASE_GRP)
    if grp:
        shutil.copyfile(grp, dest / BASE_GRP)
        rts = find_file(gamedata, RTS_FILE)
        if rts:
            shutil.copyfile(rts, dest / RTS_FILE)
        log(f"base     : {grp}")
        return
    for z in sorted(p for p in gamedata.glob("*") if p.suffix.lower() == ".zip"):
        with zipfile.ZipFile(z) as zf:
            entry = find_in_zip(zf, BASE_GRP)
            if not entry:
                continue
            for name in (BASE_GRP, RTS_FILE):
                entry = find_in_zip(zf, name)
                if entry:
                    with zf.open(entry) as fin, open(dest / name, "wb") as fout:
                        shutil.copyfileobj(fin, fout)
            log(f"base     : {z} ({BASE_GRP})")
            return
    die(
        f"{gamedata}: no {BASE_GRP} found.\n"
        "  copy the DUKE3D.GRP of your own copy of the game, version 1.4 or 1.5,\n"
        "  into gamedata/, or a .zip that has it.\n"
        "  See gamedata/README.md"
    )


def find_addons(gamedata: Path) -> list:
    """The expansion folders of gamedata/ (not '_*', not another base game)."""
    found = []
    for d in sorted(p for p in gamedata.iterdir() if p.is_dir()):
        if d.name.startswith(("_", ".")) or find_file(d, BASE_GRP):
            continue
        if any(p.is_file() for p in d.rglob("*")):
            found.append(d)
    return found


def addon_name(src: Path) -> str:
    """NWINTER for gamedata/nwinter/ or NWINTER.GRP: names its GRP in the ROM
    (the game also looks for <name>.CON in it, see n64_grp_main_con), its ROM
    duke3d-nwinter.z64 and its menu background assets/addons/nwinter.png."""
    stem = src.name if src.is_dir() else src.stem
    name = re.sub(r"[^A-Z0-9_-]", "", stem.upper())[:8]
    if not name:
        die(f"{src}: cannot make a GRP name from it, rename it (letters and digits)")
    return name


CD_IMAGE = (".bin", ".cue", ".iso")


def cd_images(files: list, work: Path) -> list:
    """The data tracks of the CD images among files: each .iso, and each .bin
    but those a .cue lists as audio tracks only. A zip holding an image is
    unpacked into work first."""
    found = []
    for p in files:
        if p.suffix.lower() == ".zip":
            with zipfile.ZipFile(p) as z:
                for i in z.infolist():
                    base = i.filename.rsplit("/", 1)[-1]
                    if Path(base).suffix.lower() in CD_IMAGE:
                        (work / "image").mkdir(parents=True, exist_ok=True)
                        with z.open(i) as fin, open(work / "image" / base, "wb") as fout:
                            shutil.copyfileobj(fin, fout)
                        found.append(work / "image" / base)
        elif p.suffix.lower() in CD_IMAGE:
            found.append(p)
    audio = set()
    for cue in (p for p in found if p.suffix.lower() == ".cue"):
        tracks = {}
        current = None
        for line in cue.read_text(encoding="latin-1").splitlines():
            words = line.split()
            if words[:1] == ["FILE"]:
                m = re.match(r'\s*FILE\s+"?(.+?)"?\s+\w+\s*$', line)
                current = (m.group(1) if m else words[1]).upper()
                tracks.setdefault(current, set())
            elif words[:1] == ["TRACK"] and current and len(words) > 2:
                tracks[current].add(words[2].upper())
        audio |= {f for f, modes in tracks.items() if modes and modes <= {"AUDIO"}}
    return [p for p in found if p.suffix.lower() in (".bin", ".iso") and p.name.upper() not in audio]


def cd_addon(images: list, work: Path) -> Path:
    """Extracts the CD images into work and keeps the expansion's files: those
    of the folders with an SSI installer or a GRP other than DUKE3D.GRP. A
    folder with a DUKE3D.GRP is a copy of the base game (compilation CDs carry
    one) and is left out, as is everything else on the CD (programs, extras)."""
    cd = work / "cd"
    for image in images:
        run_tool("cd_extract.py", image, cd)
    files = [p for p in cd.rglob("*") if p.is_file()]
    base_dirs = {p.parent for p in files if p.name.upper() == BASE_GRP}
    data_dirs = sorted({p.parent for p in files
                        if p.suffix.lower() in (".grp", ".ssi") and p.parent not in base_dirs})
    if not data_dirs:
        die(f"{images[0]}: no expansion found on the CD image (no SSI installer or GRP\n"
            "  other than DUKE3D.GRP): copy the expansion's files into its folder.\n"
            "  See gamedata/README.md")
    for p in files:
        if p.parent not in data_dirs:
            p.unlink()
    log("cd image : " + ", ".join(str(d.relative_to(cd)).replace("\\", "/") + "/" for d in data_dirs))
    return cd


def addon_source(src: Path, work: Path) -> Path:
    """What grp_pack.py gets: a folder holding just one GRP, SSI or zip is that
    file; a CD image (.bin/.cue or .iso, also in a zip), alone or in a folder
    without a GRP or SSI, is the expansion's files on it, extracted into work."""
    files = [p for p in src.iterdir() if p.is_file()] if src.is_dir() else [src]
    if not any(p.suffix.lower() in (".grp", ".ssi") for p in files):
        images = cd_images(files, work)
        if images:
            return cd_addon(images, work)
    if src.is_dir() and len(files) == 1 and files[0].suffix.lower() in (".grp", ".ssi", ".zip"):
        return files[0]
    return src


# ---------------------------------------------------------------------------
# ROM
# ---------------------------------------------------------------------------
def extract_music(grp: Path, out_dir: Path) -> None:
    data = grp.read_bytes()
    names = grp_names(data)
    offset = 16 + 16 * len(names)
    for i, name in enumerate(names):
        (size,) = struct.unpack("<I", data[28 + i * 16:32 + i * 16])
        if fnmatch.fnmatch(name, "*.MID"):
            (out_dir / name.lower()).write_bytes(data[offset:offset + size])
        offset += size


def pack(target: str, fs: Path, work: Path, out: Path, engine: Path, cfg: dict,
         tools: dict) -> Path:
    log(f"[{target}] mkdfs")
    dfs = work / f"{target}.dfs"
    run([tools["mkdfs"], dfs, fs])

    log(f"[{target}] n64tool + ed64romconfig")
    rom = work / f"{target}.z64"
    extra = sorted(engine.glob("*.version"))
    run([tools["n64tool"], "--toc", "--title", cfg["title"], "--output", rom,
         "--align", "256", engine / ENGINE_FILES[0], engine / ENGINE_FILES[1], dfs, *extra])
    run([tools["ed64romconfig"], "--savetype", cfg["savetype"],
         "--expansionpak", cfg["expansionpak"],
         *(["--regionfree"] if cfg["regionfree"] == "1" else []), rom])

    out.mkdir(parents=True, exist_ok=True)
    dest = out / f"{target}.z64"
    shutil.move(str(rom), dest)
    log(f"OK -> {dest}  ({dest.stat().st_size / 1024 / 1024:.1f} MiB)\n")
    return dest


def build_rom(addon: Path | None, con: str | None, gamedata: Path, work: Path, out: Path,
              engine: Path, cfg: dict, tools: dict, soundfont: Path) -> Path:
    name = addon_name(addon) if addon else None
    target = f"{ROM_NAME}-{name.lower()}" if name else ROM_NAME
    log(f"=== {target}.z64" + (f"  ({addon})" if addon else ""))
    tw = work / target
    fs = tw / "fs"
    fs.mkdir(parents=True)

    stage_base(gamedata, fs)
    check_atomic(fs / BASE_GRP)
    grps = [fs / BASE_GRP]
    if addon:
        # The add-on, packed into rom:/addon/<NAME>.GRP. The ROM is its own game:
        # the base GRP is trimmed to what the add-on's episodes use.
        addon_grp = fs / "addon" / f"{name}.GRP"
        addon_grp.parent.mkdir()
        run_tool("grp_pack.py", addon_source(addon, tw), addon_grp)
        full = tw / BASE_GRP
        shutil.move(str(fs / BASE_GRP), full)
        run_tool("addon_base.py", full, addon_grp, fs / BASE_GRP)
        full.unlink()
        # Read by the game at boot (n64_addon_grp, src/n64/n64_system.c).
        (fs / "addon" / "ADDON.TXT").write_text(f"{name}.GRP\n{con or ''}\n", encoding="ascii")
        grps.append(addon_grp)
        menubg = engine / "menubg" / f"{name.lower()}.sprite"
        if menubg.is_file():
            shutil.copyfile(menubg, fs / "menubg.sprite")

    # Music: the songs of the GRPs (the add-on's after the game's: same names
    # replace them), played with the General MIDI soundfont.
    midi = tw / "midi"
    midi.mkdir()
    for grp in grps:
        extract_music(grp, midi)
    songs = sorted(p.name for p in midi.glob("*.mid"))
    if songs:
        (fs / "music").mkdir()
        # Run from the MIDI folder with bare file names: audioconv64 only
        # splits paths on '/', so a Windows path would end up in the output name.
        run([tools["audioconv64"], "-o", (fs / "music").resolve(), *songs], cwd=str(midi))
    shutil.copyfile(soundfont, fs / SOUNDFONT_NAME)

    intro = engine / "intro"
    if intro.is_dir():
        shutil.copytree(intro, fs / "intro")

    return pack(target, fs, work, out, engine, cfg, tools)


def convert_soundfont(sf2: Path, work: Path, audioconv64: Path) -> Path:
    log(f"soundfont: converting {sf2}")
    out = work / "sf64"
    out.mkdir()
    run([audioconv64, "-o", out.resolve(), sf2.name], cwd=str(sf2.parent))
    converted = sorted(out.glob("*.sf64"))
    if not converted:
        die(f"audioconv64 produced no .sf64 from {sf2}")
    return converted[0]


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Pack your own Duke Nukem 3D files + the prebuilt game into Nintendo 64 ROMs.")
    ap.add_argument("--version", action="version", version=f"N64DukeNukem3DPort {project_version()}")
    ap.add_argument("targets", nargs="*",
                    help="'base', the name of an expansion folder in gamedata/, or the path "
                         "of an add-on (GRP, SSI, folder or zip). Default: the base game and "
                         "every expansion in gamedata/")
    ap.add_argument("--gamedata", type=Path, default=ROOT / "gamedata",
                    help="folder with DUKE3D.GRP and the expansion folders (default: gamedata/)")
    ap.add_argument("--out", type=Path, default=ROOT / "output",
                    help="folder the ROMs are written to (default: output/)")
    ap.add_argument("--con", default=None,
                    help="main CON script of the add-on, if the game does not pick the right one "
                         "(only with a single add-on target)")
    ap.add_argument("--soundfont", type=Path, default=None,
                    help="another General MIDI .sf2 for the music (a smaller one makes smaller ROMs)")
    ap.add_argument("--engine-dir", type=Path, default=None,
                    help="folder with the prebuilt game (default: tools/vendor/engine, "
                         "then build/prebuilt)")
    ap.add_argument("--tools-dir", type=Path, default=None,
                    help="folder with the libdragon host tools "
                         "(default: $N64_INST/bin, tools/vendor/bin, PATH)")
    ap.add_argument("--keep-work", action="store_true", help="keep the temporary work folder")
    args = ap.parse_args()

    log(f"N64DukeNukem3DPort {project_version()}")
    gamedata = args.gamedata.resolve()
    if not gamedata.is_dir():
        die(f"{gamedata}: not a folder")

    # (add-on path or None for the base game) per ROM
    roms: list = []
    for t in args.targets or ["base"] + [str(d) for d in find_addons(gamedata)]:
        if t.lower() == "base":
            roms.append(None)
        elif (gamedata / t).exists():
            roms.append(gamedata / t)
        elif Path(t).exists():
            roms.append(Path(t).resolve())
        else:
            die(f"{t}: not 'base', a folder of {gamedata} or an existing path")
    if args.con and sum(r is not None for r in roms) != 1:
        die("--con needs exactly one add-on target")

    engine = find_engine(args.engine_dir)
    cfg = read_rom_cfg(engine)
    tool_names = ["mkdfs", "n64tool", "ed64romconfig", "audioconv64"]
    tools = {t: find_tool(t, args.tools_dir) for t in tool_names}

    log(f"gamedata : {gamedata}")
    log(f"engine   : {engine}")
    for t, p in tools.items():
        log(f"tool     : {t:14s} {p}")
    log("")

    work = Path(tempfile.mkdtemp(prefix="duke3dpack-"))
    built = []
    try:
        soundfont = engine / SOUNDFONT_NAME
        if args.soundfont or not soundfont.is_file():
            sf2 = (args.soundfont or DEFAULT_SOUNDFONT).resolve()
            if not sf2.is_file():
                die(f"{sf2}: missing")
            soundfont = convert_soundfont(sf2, work, tools["audioconv64"])
        for addon in roms:
            built.append(build_rom(addon, args.con if addon else None, gamedata, work,
                                   args.out.resolve(), engine, cfg, tools, soundfont))
    finally:
        if args.keep_work:
            log(f"work folder kept: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)

    log("Built:")
    for rom in built:
        log(f"  {rom}")


if __name__ == "__main__":
    main()
