# Duke Nukem 3D for Nintendo 64

A port of the original Duke Nukem 3D (Build engine) to the N64 with
[libdragon](https://github.com/DragonMinded/libdragon) (`preview` branch), based on
[Chocolate Duke3D](https://github.com/fabiensanglard/chocolate_duke3D).
See [src/UPSTREAM.md](src/UPSTREAM.md) for where the code comes from and its licenses.

**Requires the Expansion Pak (8 MB).**

No copyrighted game data is in this repository: you provide your own copy of
Duke Nukem 3D (and its expansions), and the packer turns it into ROMs.

## What you need to play

* An **Expansion Pak** (8 MB of RAM), in the console or enabled in the emulator.
  The game does not boot without it.
* Games are saved to the cartridge FlashRAM: the flashcart or the emulator takes care
  of it.
* A flashcart (EverDrive-64, SummerCart64…) or an emulator (ares, simple64…).

> **Game Expansions: play them on a console that supports overclocking.** For Duke It Out
> In D.C., Life's A Beach and Nuclear Winter, use a console with overclocking support,
> such as the ModRetro M64 or the Analogue 3D, with overclocking turned on (or an emulator
> with overclock enabled). Their maps are bigger and busier than the base game's, and on a
> stock N64 the frame rate drops noticeably in their most complex areas. The base game
> runs fine on an unmodified N64.

## The game files

Copy the original files into [`gamedata/`](gamedata/README.md):

* `DUKE3D.GRP` (shareware 1.3D or Atomic Edition 1.4/1.5) and, if you have it, `DUKE.RTS`.
  A `.zip` of your installation that contains them works too.
* Each expansion in its own folder inside `gamedata/` (they need the Atomic Edition):

| Expansion | Folder | What to put in it | Episode | ROM |
|---|---|---|---|---|
| Nuclear Winter | `gamedata/nwinter/` | `NWINTER.GRP` | 2 | 52 MB |
| Life's A Beach | `gamedata/vacation/` | `VACATION.GRP` + its loose `GAME.CON`, `USER.CON`, `DEFS.CON` | 3 | 56 MB |
| Duke It Out In D.C. | `gamedata/dukedc/` | `DUKEDCPP.SSI` (version 1.4/1.5, from the CD) | 3 | 52 MB |

A folder can also hold the expansion's loose files or a single `.zip`.
If it comes as a CD image (`.bin`/`.iso`), `python tools/cd_extract.py image.bin
gamedata/<name>` extracts its files. Details in [gamedata/README.md](gamedata/README.md).

## How to build the ROMs

All you need is **[Python 3](https://www.python.org/downloads/)** in your `PATH`
(`python --version`). No Docker and no N64 toolchain: GitHub Actions compiles the game
on every push to `main` and publishes it in a Release, with the SoundFont, the boot logo
and the menu backgrounds already converted.

1. Download the latest [Release](../../releases) and unzip it.
2. Put your files in `gamedata/` (see above).
3. Run:
   * **Windows:** double-click `build.cmd`.
   * **Any system:** `python tools/pack_rom.py`
4. The ROMs appear in [`output/`](output/README.md): `duke3d.z64` for the base game and
   `duke3d-<folder>.z64` for each expansion, each with its own saved games.

`tools/pack_rom.py` options:

```sh
python tools/pack_rom.py base                 # only the base game
python tools/pack_rom.py nwinter              # only gamedata/nwinter/
python tools/pack_rom.py path/MYMAPS.zip --con MYMAPS.CON   # user maps
python tools/pack_rom.py --soundfont path/small.sf2         # smaller ROMs
```

The Release ships the tools for Windows and Linux x86-64; on other systems they are
built with `tools/vendor/bin/build-tools.sh` (see
[tools/vendor/bin/README.md](tools/vendor/bin/README.md)).

### How the expansions work

- The ROM only has the expansion's episodes (those with maps in it, according to its
  `definelevelname`); if there is only one, the menu goes straight to the skill choice.
- What is not used is removed from the base GRP (`tools/addon_base.py`): the files the
  expansion replaces, the maps and animations of the other episodes, the demos, and the
  base CONs if the expansion brings its own. Nuclear Winter: 52 MB instead of 75.
- The CON script is picked automatically: `GAME.CON` if the expansion has one, otherwise
  the one named like the GRP (`NWINTER.CON` in `NWINTER.GRP`), otherwise its only `.CON`
  besides `DEFS.CON`/`USER.CON`. If it guesses wrong: `--con NAME.CON`.
- The compiled game is the same for every ROM: an expansion's ROM carries its GRP in
  `rom:/addon/` and its name in `rom:/addon/ADDON.TXT`, which the game reads at boot.
- Main menu background: `assets/addons/<folder>.png` (320×200, 256 colors), made from
  any 4:3 image with `python tools/menubg.py image.jpg assets/addons/<folder>.png`
  (needs Pillow). Nuclear Winter, Life's A Beach and D.C. already have one. Without an
  image, the menu uses the game's title screen, like the base game.

## Building from source

Working on the code needs the N64 toolchain, which runs in Docker through the
`libdragon` CLI:

* **[Docker](https://www.docker.com/)**
* **The `libdragon` CLI** (`npm install -g libdragon`, see `.libdragon/config.json`)

```sh
git clone --recurse-submodules <repository-url>
cd N64DukeNukem3DPort
git -C libdragon apply ../patches/libdragon-mixer-clamp-frequency.patch   # see patches/
libdragon install        # the first time: builds the pinned libdragon in the container
```

With the files in `gamedata/`:

```sh
libdragon make                          # output/duke3d.z64
libdragon make ADDON=gamedata/nwinter   # output/duke3d-nwinter.z64 (ADDON_CON=NAME.CON)
libdragon make roms                     # the base game and every expansion
libdragon make engine                   # only the compiled game + media: build/prebuilt/
```

`make` compiles the game (`make engine`) and then calls `tools/pack_rom.py`, the same
packer as the Release. The project version is in `VERSION`: every push to `main`
publishes (or updates) the `v<VERSION>` Release.

## Controls

**In game**

| N64 controller | Action |
|---|---|
| Stick up / down | Walk forward / back |
| Stick left / right | Turn (analog: gentle near the center, fast at the edge) |
| Z | Fire |
| R | Open / use |
| A | Jump |
| B (hold) | Run (with "RUN ALWAYS" on in Game Options: walk) |
| R + B | Quick kick |
| C-left / C-right | Strafe |
| C-down | Crouch |
| C-up | Center the view |
| R + C-up / C-down | Look (aim) up / down |
| L | Use the inventory item |
| D-pad left / right | Previous / next weapon |
| D-pad up / down | Previous / next item |
| Start | Menu |

**In menus:** stick or D-pad to move, A to accept, B or Start to go back.


## Debugging

- `libdragon make ARGS="/v1 /l1"`: boots straight into episode 1, level 1 (any DOS
  command line argument).
- `libdragon make clean && libdragon make PROFILE=1`: every second, writes to the log the
  FPS, the milliseconds per frame of each render phase and the audio peak with the active
  voices, plus the libdragon profiler (mixer, music) every 5 s.
- `libdragon make INPUT="8000:A 9500:A 12000-14000:U"`: simulated controller for tests
  without a player (A button at 8 and 9.5 s, stick up from 12 to 14 s). Buttons:
  `A B Z L R S`, stick `U D < >`, C buttons `u d l r`.
- A normal build (without `ARGS`/`INPUT`) never keeps those test options.
- `libdragon make RDP_COLUMNS=0`: walls and sky are drawn on the CPU again (by default the
  RDP draws them, see `src/n64/n64_columns.h`), for comparison.
- `libdragon make GOD=1`: the player does not die (for measuring: the dead player's view is
  drawn differently and skews the measurements).
- `libdragon make LEVELSKIP=8`: moves on to the next level by itself every 8 s (with `GOD`),
  and `LEVELCYCLE=3` repeats the first 3 of the episode. With the
  `memory E1L2: heap used ...` line the log writes on each level, it checks for memory leaks.
- `libdragon make QUAKE=6`: every 6 s, in turn: nothing, an earthquake, nothing, the screen
  tilted, nothing, explosions with debris in front of the player; to compare their profiles.
- `libdragon make DETONATE=253`: 8 s into the level, sets off the C-9s with that hitag, as
  if they had been shot (explosions that change the map).
- ares shows the log (`printf`/`debugf`) on its standard output.

## License

- Game code: GPL v2 or later (see [LICENSE](LICENSE)).
- Build engine: Ken Silverman's license ([BUILDLIC.TXT](BUILDLIC.TXT)), which only allows
  distributing derived works for free.
- Duke Nukem 3D and its data belong to their owners and are not included here.

More details in [src/UPSTREAM.md](src/UPSTREAM.md).

## A note on AI

This project was developed with the help of AI. I am just a hobbyist who wanted to make
interesting projects; in this case, how it was achieved is not what matters to me.
