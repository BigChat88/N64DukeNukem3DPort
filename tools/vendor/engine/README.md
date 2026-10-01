# tools/vendor/engine

El juego precompilado que `tools/pack_rom.py` mete en cada ROM:

| archivo | qué es |
|---|---|
| `duke3d.elf.stripped` | el juego compilado (ELF comprimido) |
| `duke3d.elf.sym` | símbolos para las trazas de los errores |
| `soundfont.sf64` | el SoundFont General MIDI de la música, ya convertido |
| `intro/*.sprite` | el logo "powered by libdragon" del arranque |
| `menubg/*.sprite` | el fondo del menú de cada expansión (`assets/addons/*.png`) |
| `libdragon.version` | versión de libdragon |
| `rom.cfg` | cabecera de la ROM (título, guardado, Expansion Pak) |

No contienen datos del juego y son iguales para todos: la misma compilación sirve para el
juego base y para todas las expansiones (la ROM de una expansión lleva su nombre en
`rom:/addon/ADDON.TXT`).

Se obtienen de:

* **Una [Release](../../../../releases)**: GitHub Actions los compila en cada push a `main`
  y los incluye en esta carpeta.
* **`make engine`** en la raíz, dentro de `libdragon exec` (necesita Docker y el CLI
  `libdragon`). Quedan en `build/prebuilt/`, donde `pack_rom.py` también busca.
