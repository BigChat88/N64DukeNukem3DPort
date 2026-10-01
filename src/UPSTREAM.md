# Where the code comes from

`src/engine` and `src/game` come from **Chocolate Duke3D**
(https://github.com/fabiensanglard/chocolate_duke3D, commit `1cfa190`, 2019-03-08),
the Duke Nukem 3D v1.5 source code released by 3D Realms in 2003, cleaned up and ported
to SDL.

To see what changed from the original, compare with that commit: every N64 adaptation is
marked with `PLATFORM_N64` / `N64`.

## Licenses

- Game code (`src/game`): **GPL v2 or later** (3D Realms).
- Build engine (`src/engine`): **Ken Silverman's license**, see `BUILDLIC.TXT`.
  It only allows distributing derived works **for free and over the Internet**
  (no sales and no physical cartridges) and requires keeping its copyright notices.
- The game data (`DUKE3D.GRP`) is **not** free and is not included.
- "powered by libdragon" intro (`src/n64/n64_intro.c`, `assets/intro/*.png`): MIT,
  N64brew-GameJam2024 via lambertjamesd/n64brew2025, ported from the N64 Doom port.
- SoundFont `assets/soundfont/GeneralUser-GS.sf2` (S. Christian Collins): free use and
  redistribution, see `assets/soundfont/LICENSE.txt`.

## Original files not compiled on the N64

| File | Replaced by |
|---|---|
| `engine/display.c` (SDL driver) | `n64/n64_display.c` |
| `engine/mmulti.c` (UDP/enet networking) | nothing: no networking (`USER_DUMMY_NETWORK`) |
| `engine/unix_compat.h` | `engine/n64_compat.h` |
| `game/audiolib/fx_man.c`, `multivoc.c`, `mv_mix.c`, `mvreverb.c`, `dsl.c`, `ll_man.c`, `nodpmi.c`, `user.c` (CPU mixer + SDL_mixer) | `n64/n64_fx.c` (`FX_*` API on top of the libdragon RSP mixer; only `pitch.c` is used) |
| `game/audiolib/usrhooks.c` | duplicate of functions in `game/sounds.c` |
| `game/midi/sdl_midi.c` (not copied) | `n64/n64_music.c` (libdragon MID64 + SF64 SoundFont) |
| `game/dukeunix.h` | `game/duken64.h` |

`n64/SDL.h` replaces the few SDL calls left in the game with empty functions.

## Changes to the original code

- `engine/engine.c`: `transluc` is read as bytes (before, as byte-swapped `int32`, which
  corrupted it on big-endian).
- `engine/tiles.c`: texture cache limited to `N64_TILE_CACHE_SIZE`; the ART headers are
  read at once and decoded in memory (before, thousands of 2-4 byte reads).
- `game/gamedef.c`: the CON compiler looks up keywords and labels with hash tables
  (`con_keyword_first`, `con_label_first`) instead of walking the lists with `strcmp` on
  every word: compiling GAME.CON goes from 6.7 s to 0.3 s on the N64, with the same result.
- `engine/filesystem.c`: no GRP CRC (avoids reading ~26 MB at boot and a 1 MB buffer).
- `engine/build.h`: smaller `MAXXDIM`/`MAXYDIM`.
- `game/game.c`: the GRP is looked up in `rom:/`, with no executable CRC and no keyboard
  prompts.
- `game/global.c`: `Error()` shows the message on screen; empty `_dos_findfirst`.
- `game/audiolib/multivoc.c`, `game/sounds.c`: VOC/WAV headers read byte by byte as
  little-endian (before: misaligned 16/32-bit reads, which the VR4300 does not allow).
- `game/config.c`: on the N64, 8 voices at 22,050 Hz (Chocolate forced 32 at 44,100 Hz)
  and no 0.75 s wait when reading the configuration.
- `game/game.c`: the episode 1 ending animation read past the end of the `bossmove` array.
- `engine/fixedPoint_math.c`: `clearbufbyte` repeats the pattern in the machine's byte
  order (it assumed little-endian: on the N64 the wall column limits, `mostbuf`, had their
  bytes swapped and pieces of wall showed up where they should not).
- `int32` passed as `short*` (only works on little-endian): `game/gamedef.c` (`pushmove`
  when an actor falls: dead enemies ended up outside their sector) and the volume, screen
  size and brightness bars of `game/menues.c` (`bar32`).
- Saved games on the N64 (`game/menues.c`, `game/config.c`, `engine/filesystem.c`): files
  in `save:/` (FlashRAM, `n64/n64_savefs.c`); without the compiled CON script (it is
  recompiled at boot; the CON CRC is part of the version, `SAVE_BYTEVERSION`); only the
  walls and sectors in use; free sprites and actors zeroed; `dfwrite`/`dfread` keep the
  delta encoding but without the LZW (misaligned reads).
- `game/game.c` (`animatesprites`): an actor's current action was recognized with
  `t4 > 10000`, which on the N64 (addresses 0x80xxxxxx, negative as `int32`) was never
  true: action frames (walking, dying...) were not applied. Now it checks that the address
  is inside `script[]`.
- `game/animlib.c` (ANM animations): headers and page tables converted from little-endian
  when loaded, and misaligned 16-bit words read byte by byte.
- `engine/engine.c` (`wallscan`) and `game/game.c` (`displayrooms`): on the N64 the RDP
  draws the main view's walls and sky (`n64_rdpscan`, `n64/n64_columns.h`): they are
  recorded per column, the RDP marks their holes in the 8-bit buffer and textures them when
  presenting. Several drawing functions wait for the RDP (`n64_wait_rdp`) because sending
  the frame no longer waits for it.
- `engine/engine.c` (`dorotatesprite`, `drawsprite`, `drawmasks`) and `game/game.c`
  (`displayrooms`, `displayrest`): on the N64 the RDP draws over the frame the unrotated
  (or half-turned, i.e. mirrored) 2D sprites (weapon, crosshair, menus, HUD) and the face
  sprites nearer than every masked wall and than the wall or floor sprites they share
  columns with, clipped per column (`n64/n64_overlay.h`). Also the ANM animations
  (`game/menues.c`, `playanm`) and the screen tilt (`displayrooms`, `n64_view_tilt`), which
  the original did by rendering to a tile and rotating it on the CPU.
- `engine/tiles.c`: on the N64, textures are written back from the CPU cache when loaded.
- `game/game.c`: demos disabled on the N64 (their reader puts bytes and `short`s into
  `int32` fields, and the recorded input is little-endian).
- `game/premap.c`: the wait for the skill voice to end keeps the audio running
  (`getpackets()`); it used to be an empty loop that never ended on the N64.
- `engine/draw.c`: the inner loops copy globals into local variables (the `uint8_t*` could
  point to any global and forced re-reading them on every pixel); the `pixelsAllowed` debug
  counter does not exist on the N64.

## Changes to libdragon (submodule)

- `src/audio/mixer.c` (`mixer_ch_apply_freq`): a frequency above the channel's limit is
  clamped to the limit instead of aborting. The music synthesizer (`sf64_synth`) goes over
  it on some high notes with a pitch envelope (e.g. the episode 3 music), and raising the
  limit of the music channels would cost ~800 KB of RAM. After changing it:
  `libdragon install`.
