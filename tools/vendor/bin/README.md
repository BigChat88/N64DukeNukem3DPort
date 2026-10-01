# tools/vendor/bin

Las herramientas de libdragon que ejecuta `tools/pack_rom.py`:

| herramienta | función |
|---|---|
| `mkdfs` | empaqueta los archivos del juego en el sistema de archivos DragonFS |
| `n64tool` | une juego compilado + símbolos + sistema de archivos en el `.z64` |
| `ed64romconfig` | escribe el tipo de guardado y el Expansion Pak en la cabecera de la ROM |
| `audioconv64` | convierte la música MIDI del GRP a MID64 |

`pack_rom.py` las busca en `--tools-dir`, `$N64_INST/bin`, esta carpeta y el `PATH`.

## Cómo conseguirlas

* **Descarga una [Release](../../../../releases)**: trae aquí los `.exe` de Windows y los
  binarios de Linux x86-64, junto con el juego compilado (ver `../engine/README.md`).
* **Compílalas** desde el submódulo `libdragon` con un compilador de C y C++ (sin Docker):

  ```sh
  ./build-tools.sh                                                        # gcc/g++ del PATH
  CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ ./build-tools.sh   # para Windows
  ```
