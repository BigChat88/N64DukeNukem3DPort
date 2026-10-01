# tools/vendor/bin

The libdragon tools that `tools/pack_rom.py` runs:

| tool | what it does |
|---|---|
| `mkdfs` | packs the game files into the DragonFS file system |
| `n64tool` | joins compiled game + symbols + file system into the `.z64` |
| `ed64romconfig` | writes the save type and the Expansion Pak into the ROM header |
| `audioconv64` | converts the GRP's MIDI music to MID64 |

`pack_rom.py` looks for them in `--tools-dir`, `$N64_INST/bin`, this folder and the `PATH`.

## How to get them

* **Download a [Release](../../../../releases)**: it puts the Windows `.exe` files and the
  Linux x86-64 binaries here, along with the compiled game (see `../engine/README.md`).
* **Build them** from the `libdragon` submodule with a C and C++ compiler (no Docker):

  ```sh
  ./build-tools.sh                                                        # gcc/g++ from PATH
  CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ ./build-tools.sh   # for Windows
  ```
