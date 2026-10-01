# Duke Nukem 3D para Nintendo 64

Port del Duke Nukem 3D original (motor Build) a N64 con [libdragon](https://github.com/DragonMinded/libdragon)
(rama `preview`), a partir de [Chocolate Duke3D](https://github.com/fabiensanglard/chocolate_duke3D).
Ver [src/UPSTREAM.md](src/UPSTREAM.md) para el origen del código y las licencias.

**Requiere Expansion Pak (8 MB).**

Ningún dato del juego con copyright está en este repositorio: tú pones tu copia de
Duke Nukem 3D (y de sus expansiones) y el empaquetador la convierte en ROMs.

## Qué necesitas para jugar

* Un **Expansion Pak** (8 MB de RAM), en la consola o activado en el emulador.
  Sin él el juego no arranca.
* Las partidas se guardan en la FlashRAM del cartucho: el flashcart o el emulador se
  encargan solo.
* Un flashcart (EverDrive-64, SummerCart64…) o un emulador (ares, simple64…).

> **Expansiones: recomendadas para N64 con overclock.** Los mapas de Duke It Out In D.C.,
> Life's A Beach y Nuclear Winter son más grandes y cargados que los del juego base, y en
> una N64 de serie la tasa de fotogramas baja bastante en sus zonas más complejas. Se
> juegan, pero lo recomendable es una consola modificada con overclock de la CPU (o un
> emulador con overclock activado). El juego base es el que mejor funciona en una N64
> sin modificar.

## Los archivos del juego

Copia en [`gamedata/`](gamedata/README.md) los archivos originales:

* `DUKE3D.GRP` (shareware 1.3D o Atomic Edition 1.4/1.5) y, si lo tienes, `DUKE.RTS`.
  Vale también un `.zip` de tu instalación que los contenga.
* Cada expansión en su propia carpeta dentro de `gamedata/` (necesitan la Atomic Edition):

| Expansión | Carpeta | Qué poner dentro | Episodio | ROM |
|---|---|---|---|---|
| Nuclear Winter | `gamedata/nwinter/` | `NWINTER.GRP` | 2 | 52 MB |
| Life's A Beach | `gamedata/vacation/` | `VACATION.GRP` + sus `GAME.CON`, `USER.CON`, `DEFS.CON` sueltos | 3 | 56 MB |
| Duke It Out In D.C. | `gamedata/dukedc/` | `DUKEDCPP.SSI` (versión 1.4/1.5, del CD) | 3 | 52 MB |

Una carpeta puede tener también los archivos sueltos de la expansión o un único `.zip`.
Si viene en imagen de CD (`.bin`/`.iso`), `python tools/cd_extract.py imagen.bin
gamedata/<nombre>` saca sus archivos. Detalles en [gamedata/README.md](gamedata/README.md).

## Cómo generar las ROMs

Solo hace falta **[Python 3](https://www.python.org/downloads/)** en el `PATH`
(`python --version`). Ni Docker ni el toolchain de N64: GitHub Actions compila el juego
en cada push a `main` y lo publica en una Release, con el SoundFont, el logo de arranque
y los fondos de menú ya convertidos.

1. Descarga la última [Release](../../releases) y descomprímela.
2. Pon tus archivos en `gamedata/` (ver arriba).
3. Ejecuta:
   * **Windows:** doble clic en `build.cmd`.
   * **Cualquier sistema:** `python tools/pack_rom.py`
4. Las ROMs aparecen en [`output/`](output/README.md): `duke3d.z64` para el juego base y
   `duke3d-<carpeta>.z64` para cada expansión, cada una con sus propias partidas guardadas.

Opciones de `tools/pack_rom.py`:

```sh
python tools/pack_rom.py base                 # solo el juego base
python tools/pack_rom.py nwinter              # solo gamedata/nwinter/
python tools/pack_rom.py ruta/MISMAPAS.zip --con MISMAPAS.CON   # mapas de usuario
python tools/pack_rom.py --soundfont ruta/pequeno.sf2           # ROMs más pequeñas
```

La Release trae las herramientas para Windows y Linux x86-64; en otros sistemas se
compilan con `tools/vendor/bin/build-tools.sh` (ver
[tools/vendor/bin/README.md](tools/vendor/bin/README.md)).

### Cómo funcionan las expansiones

- La ROM solo tiene los episodios de la expansión (los que tienen mapas en ella, según sus
  `definelevelname`); si es uno solo, el menú pasa directamente a elegir la dificultad.
- Del GRP base se quita lo que no se usa (`tools/addon_base.py`): los archivos que la
  expansión sustituye, los mapas y las animaciones de los otros episodios, las demos y los
  CON base si la expansión trae los suyos. Nuclear Winter: 52 MB en vez de 75.
- El script CON se elige solo: `GAME.CON` si la expansión trae uno, si no el que se llama
  como el GRP (`NWINTER.CON` en `NWINTER.GRP`), si no su único `.CON` aparte de
  `DEFS.CON`/`USER.CON`. Si no acierta: `--con NOMBRE.CON`.
- El juego compilado es el mismo para todas las ROMs: la de una expansión lleva su GRP en
  `rom:/addon/` y su nombre en `rom:/addon/ADDON.TXT`, que el juego lee al arrancar.
- Fondo del menú principal: `assets/addons/<carpeta>.png` (320×200, 256 colores), hecho a
  partir de cualquier imagen 4:3 con `python tools/menubg.py imagen.jpg
  assets/addons/<carpeta>.png` (necesita Pillow). Ya están los de Nuclear Winter, Life's
  A Beach y D.C. Sin imagen, el menú usa la pantalla de título del juego, como el base.

## Compilar desde el código fuente

Para trabajar en el código hace falta el toolchain de N64, que va en Docker mediante el
CLI `libdragon`:

* **[Docker](https://www.docker.com/)**
* **El CLI `libdragon`** (`npm install -g libdragon`, ver `.libdragon/config.json`)

```sh
git clone --recurse-submodules <url-del-repositorio>
cd N64DukeNukem3DPort
git -C libdragon apply ../patches/libdragon-mixer-clamp-frequency.patch   # ver patches/
libdragon install        # la primera vez: compila el libdragon fijado en el contenedor
```

Con los archivos en `gamedata/`:

```sh
libdragon make                          # output/duke3d.z64
libdragon make ADDON=gamedata/nwinter   # output/duke3d-nwinter.z64 (ADDON_CON=NOMBRE.CON)
libdragon make roms                     # el juego base y todas las expansiones
libdragon make engine                   # solo el juego compilado + multimedia: build/prebuilt/
```

`make` compila el juego (`make engine`) y después llama a `tools/pack_rom.py`, el mismo
empaquetador de la Release. La versión del proyecto está en `VERSION`: cada push a `main`
publica (o actualiza) la Release `v<VERSION>`.

## Controles

**En la partida**

| Mando N64 | Acción |
|---|---|
| Stick arriba / abajo | Andar adelante / atrás |
| Stick izquierda / derecha | Girar (analógico: suave cerca del centro, rápido al fondo) |
| Z | Disparar |
| R | Abrir / usar |
| A | Saltar |
| B (mantener) | Correr (con "RUN ALWAYS" activado en Game Options: andar) |
| R + B | Patada rápida |
| C-izq / C-der | Paso lateral |
| C-abajo | Agacharse |
| C-arriba | Centrar la vista |
| R + C-arriba / C-abajo | Mirar (apuntar) arriba / abajo |
| L | Usar el objeto del inventario |
| Cruceta izq / der | Arma anterior / siguiente |
| Cruceta arriba / abajo | Objeto anterior / siguiente |
| Start | Menú |

**En los menús:** stick o cruceta para moverse, A para aceptar, B o Start para volver.

## Estado

- [x] Compila y enlaza con libdragon `preview`.
- [x] Arranca, monta la ROM y muestra errores en pantalla.
- [x] Carga el GRP (probado con el shareware 1.3D), menús y niveles (E1L1 en ares).
- [x] Efectos de sonido mezclados en el RSP (~1,5 ms de CPU por fotograma). Verificado por
  el pico de la señal de salida en el log; falta escucharlo en hardware.
- [ ] Reverberación (la del original era un efecto por CPU; no implementada).
- [x] Música MIDI: las canciones del GRP se convierten al compilar y suenan con un
  SoundFont General MIDI (GeneralUser GS) mezclado en el RSP. Pausar reinicia la canción.
- [x] Partidas guardadas en la FlashRAM del cartucho (128 KB): 3 ranuras. Al elegir una
  ranura se guarda al momento (nombre DUKE1..DUKE3, no hay teclado). Cada partida guarda solo
  lo que cambió respecto al mapa y se comprime (~40 KB en E1L1, 130 KB sin esto); si un nivel
  muy cargado no deja sitio para la tercera, sale "NOT ENOUGH SAVE SPACE" y se puede
  sobrescribir otra. Solo sirven con el mismo GRP/CON con el que se guardaron.
- [ ] Demos (desactivadas: su formato asume little-endian).
- [ ] Stick analógico real (ahora se traduce a teclas).
- [ ] Rendimiento: ~16 FPS en el inicio de E1L1 en ares (con música y animaciones).
  Reparto por fotograma: suelos 10 ms, cielo 9,5, sprites 9,5, lógica 9, geometría 6,
  paredes 4, audio 4, rampas 3,5.

## Depuración

- `libdragon make ARGS="/v1 /l1"`: arranca directamente en el episodio 1, nivel 1
  (cualquier argumento de la línea de comandos de DOS).
- `libdragon make clean && libdragon make PROFILE=1`: escribe cada segundo en el log
  los FPS, los milisegundos por fotograma de cada fase del render y el pico de audio
  con las voces activas, más el perfilador de libdragon (mezclador, música) cada 5 s.
- `libdragon make INPUT="8000:A 9500:A 12000-14000:U"`: mando simulado para pruebas sin
  jugador (botón A a los 8 y 9,5 s, stick arriba de 12 a 14 s). Botones: `A B Z L R S`,
  stick `U D < >`, botones C `u d l r`.
- Una compilación normal (sin `ARGS`/`INPUT`) nunca conserva esas opciones de prueba.
- `libdragon make RDP_COLUMNS=0`: paredes y cielo vuelven a dibujarse en la CPU (por defecto
  los dibuja el RDP, ver `src/n64/n64_columns.h`), para comparar.
- `libdragon make GOD=1`: el jugador no muere (para medir: la vista de muerto se dibuja de
  otra forma y falsea las medidas).
- `libdragon make LEVELSKIP=8`: pasa solo al siguiente nivel cada 8 s (con `GOD`), y
  `LEVELCYCLE=3` repite los 3 primeros del episodio. Con la línea `memory E1L2: heap used ...`
  que el log escribe en cada nivel, sirve para comprobar que no hay fugas de memoria.
- ares muestra el log (`printf`/`debugf`) en su salida estándar.

## Arquitectura (N64)

- `src/n64/n64_display.c`: Build dibuja en un búfer de 8 bits de 320×200. El RDP lo copia
  al framebuffer como textura CI8 con la paleta en TMEM, así que la conversión de color
  no gasta CPU. El VI estira 320×200 a 4:3, como el modo 13h de VGA.
  Encima, el RDP dibuja el arma, la mira, los menús, el HUD y los sprites de cara del mundo
  (enemigos, objetos) que no quedan detrás de una pared enmascarada (`src/n64/n64_overlay.h`).
- `src/n64/n64_fx.c`: la API de efectos del juego (`FX_*`) sobre el mezclador de libdragon.
  Cada voz es un canal mezclado por el RSP; la CPU solo convierte las muestras (8 bits sin
  signo / 16 bits little-endian) y conserva el paneo 3D, volúmenes y prioridades del original.
- `src/n64/n64_system.c`: arranque (DragonFS, comprobación del Expansion Pak) y pantalla de error.

## Licencia

- Código del juego: GPL v2 o posterior (ver [LICENSE](LICENSE)).
- Motor Build: licencia de Ken Silverman ([BUILDLIC.TXT](BUILDLIC.TXT)), que solo permite
  distribuir obras derivadas gratis.
- Duke Nukem 3D y sus datos pertenecen a sus propietarios y no se incluyen aquí.

Más detalles en [src/UPSTREAM.md](src/UPSTREAM.md).

## Nota sobre IA

El proyecto se ha desarrollado con ayuda de IA. Solo soy un aficionado que quería hacer
proyectos interesantes; en este caso, cómo se ha conseguido no es lo importante para mí.
