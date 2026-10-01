# tools/vendor/engine

The precompiled game that `tools/pack_rom.py` puts into every ROM:

| file | what it is |
|---|---|
| `duke3d.elf.stripped` | the compiled game (compressed ELF) |
| `duke3d.elf.sym` | symbols for error backtraces |
| `soundfont.sf64` | the music's General MIDI SoundFont, already converted |
| `intro/*.sprite` | the "powered by libdragon" boot logo |
| `menubg/*.sprite` | each expansion's menu background (`assets/addons/*.png`) |
| `libdragon.version` | libdragon version |
| `rom.cfg` | ROM header (title, save type, Expansion Pak) |

They hold no game data and are the same for everyone: the same build serves the base game
and every expansion (an expansion's ROM carries its name in `rom:/addon/ADDON.TXT`).

Get them from:

* **A [Release](../../../../releases)**: GitHub Actions builds them on every push to
  `main` and includes them in this folder.
* **`make engine`** at the root, inside `libdragon exec` (needs Docker and the `libdragon`
  CLI). They end up in `build/prebuilt/`, where `pack_rom.py` also looks.
