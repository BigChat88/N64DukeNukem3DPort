# Origen del código

`src/engine` y `src/game` vienen de **Chocolate Duke3D**
(https://github.com/fabiensanglard/chocolate_duke3D, commit `1cfa190`, 2019-03-08),
el código de Duke Nukem 3D v1.5 publicado por 3D Realms en 2003, limpiado y portado a SDL.

Para ver qué se ha cambiado respecto al original, compara con ese commit:
todas las adaptaciones a N64 están marcadas con `PLATFORM_N64` / `N64`.

## Licencias

- Código del juego (`src/game`): **GPL v2 o posterior** (3D Realms).
- Motor Build (`src/engine`): **licencia de Ken Silverman**, ver `BUILDLIC.TXT`.
  Solo permite distribuir obras derivadas **gratis y por Internet**
  (nada de venta ni cartuchos físicos) y exige mantener sus avisos de copyright.
- Los datos del juego (`DUKE3D.GRP`) **no** son libres y no se incluyen.
- Intro "powered by libdragon" (`src/n64/n64_intro.c`, `assets/intro/*.png`): MIT,
  N64brew-GameJam2024 vía lambertjamesd/n64brew2025, portada desde el port de Doom a N64.
- SoundFont `assets/soundfont/GeneralUser-GS.sf2` (S. Christian Collins): uso y
  redistribución libres, ver `assets/soundfont/LICENSE.txt`.

## Archivos originales que no se compilan en N64

| Archivo | Sustituido por |
|---|---|
| `engine/display.c` (driver SDL) | `n64/n64_display.c` |
| `engine/mmulti.c` (red UDP/enet) | nada: sin red (`USER_DUMMY_NETWORK`) |
| `engine/unix_compat.h` | `engine/n64_compat.h` |
| `game/audiolib/fx_man.c`, `multivoc.c`, `mv_mix.c`, `mvreverb.c`, `dsl.c`, `ll_man.c`, `nodpmi.c`, `user.c` (mezclador por CPU + SDL_mixer) | `n64/n64_fx.c` (API `FX_*` sobre el mezclador RSP de libdragon; solo se usa `pitch.c`) |
| `game/audiolib/usrhooks.c` | duplicado de funciones de `game/sounds.c` |
| `game/midi/sdl_midi.c` (no copiado) | `n64/n64_music.c` (MID64 + SoundFont SF64 de libdragon) |
| `game/dukeunix.h` | `game/duken64.h` |

`n64/SDL.h` sustituye las pocas llamadas a SDL que quedan en el juego por funciones vacías.

## Cambios en el código original

- `engine/engine.c`: `transluc` se lee como bytes (antes, como `int32` con
  intercambio de bytes, lo que lo corrompía en big-endian).
- `engine/tiles.c`: caché de texturas limitada a `N64_TILE_CACHE_SIZE`; las cabeceras de
  los ART se leen de una vez y se decodifican en memoria (antes, miles de lecturas de
  2-4 bytes).
- `game/gamedef.c`: el compilador de CON busca palabras clave y etiquetas con tablas hash
  (`con_keyword_first`, `con_label_first`) en vez de recorrer las listas con `strcmp`
  en cada palabra: compilar GAME.CON pasa de 6,7 s a 0,3 s en N64, con el mismo resultado.
- `engine/filesystem.c`: sin CRC del GRP (evita leer ~26 MB al arrancar y un búfer de 1 MB).
- `engine/build.h`: `MAXXDIM`/`MAXYDIM` reducidos.
- `game/game.c`: el GRP se busca en `rom:/`, sin CRC del ejecutable ni preguntas por teclado.
- `game/global.c`: `Error()` muestra el mensaje en pantalla; `_dos_findfirst` vacío.
- `game/audiolib/multivoc.c`, `game/sounds.c`: cabeceras VOC/WAV leídas byte a byte en
  little-endian (antes: lecturas de 16/32 bits desalineadas, que el VR4300 no admite).
- `game/config.c`: en N64, 8 voces a 22 050 Hz (Chocolate forzaba 32 a 44 100 Hz) y sin
  la espera de 0,75 s al leer la configuración.
- `game/game.c`: la animación final del episodio 1 leía fuera del array `bossmove`.
- `engine/fixedPoint_math.c`: `clearbufbyte` repite el patrón en el orden de bytes de la
  máquina (antes asumía little-endian: en N64 los límites de columna de las paredes,
  `mostbuf`, quedaban con bytes invertidos y aparecían trozos de pared donde no debían).
- `int32` pasados como `short*` (solo funciona en little-endian): `game/gamedef.c`
  (`pushmove` al caer un actor: los enemigos muertos acababan fuera de su sector) y las
  barras de volumen, tamaño de pantalla y brillo de `game/menues.c` (`bar32`).
- Partidas guardadas en N64 (`game/menues.c`, `game/config.c`, `engine/filesystem.c`):
  archivos en `save:/` (FlashRAM, `n64/n64_savefs.c`); sin el script CON compilado (se
  recompila al arrancar; el CRC de los CON va en la versión, `SAVE_BYTEVERSION`); solo las
  paredes y sectores en uso; sprites y actores libres a cero; `dfwrite`/`dfread` conservan
  la codificación por diferencias pero sin el LZW (lecturas desalineadas).
- `game/game.c` (`animatesprites`): la acción actual de un actor se reconocía con
  `t4 > 10000`, que en N64 (direcciones 0x80xxxxxx, negativas como `int32`) nunca se
  cumplía: no se aplicaban los fotogramas de las acciones (andar, morir...). Ahora se
  comprueba que la dirección está dentro de `script[]`.
- `game/animlib.c` (animaciones ANM): cabeceras y tablas de páginas convertidas de
  little-endian al cargarlas, y palabras de 16 bits desalineadas leídas byte a byte.
- `engine/engine.c` (`wallscan`) y `game/game.c` (`displayrooms`): en N64 las paredes y el
  cielo de la vista principal los dibuja el RDP (`n64_rdpscan`, `n64/n64_columns.h`): se
  registran por columnas, el RDP marca sus huecos en el búfer de 8 bits y los texturiza al
  presentar. Varias funciones de dibujo esperan al RDP (`n64_wait_rdp`) porque el envío del
  fotograma ya no lo espera.
- `engine/engine.c` (`dorotatesprite`, `drawsprite`, `drawmasks`) y `game/game.c`
  (`displayrooms`, `displayrest`): en N64 el RDP dibuja sobre el fotograma los sprites 2D sin
  rotar (arma, mira, menús, HUD) y los sprites de cara más cercanos que toda pared enmascarada
  y los sprites de pared o suelo con los que comparten columnas, recortados por columnas
  (`n64/n64_overlay.h`). También las animaciones ANM (`game/menues.c`, `playanm`) y la
  inclinación de pantalla (`displayrooms`, `n64_view_tilt`), que el original hacía
  renderizando a un tile y rotándolo en la CPU.
- `engine/tiles.c`: en N64, las texturas se vuelcan de la caché de la CPU al cargarlas.
- `game/game.c`: demos desactivadas en N64 (su lector mete bytes y `short` en campos `int32`
  y la entrada grabada es little-endian).
- `game/premap.c`: la espera a que termine la voz de la dificultad hace avanzar el audio
  (`getpackets()`); antes era un bucle vacío que en N64 no terminaba nunca.
- `engine/draw.c`: los bucles internos copian las globales a variables locales (los
  `uint8_t*` pueden apuntar a cualquier global y obligaban a releerlas en cada píxel);
  el contador de depuración `pixelsAllowed` no existe en N64.

## Cambios en libdragon (submódulo)

- `src/audio/mixer.c` (`mixer_ch_apply_freq`): una frecuencia por encima del límite del canal
  se recorta al límite en vez de abortar. El sintetizador de música (`sf64_synth`) la supera
  en algunas notas agudas con envolvente de tono (p. ej. la música del episodio 3), y subir
  el límite de los canales de música costaría ~800 KB de RAM. Tras cambiarlo:
  `libdragon install`.
