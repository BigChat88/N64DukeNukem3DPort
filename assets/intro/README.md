# assets/intro

Line-art layers of the "powered by libdragon" dragon logo played at boot
(`src/n64/n64_intro.c`): `dragon1.png` head, `dragon2.png` body, `dragon3.png`
tail, `dragon4.png` wordmark.

They come from [lambertjamesd/n64brew2025](https://github.com/lambertjamesd/n64brew2025)
(`assets/images/intro/`), which sourced them from the N64brew-GameJam2024
repository (MIT license, see the header of `src/n64/n64_intro.c`). Copied from
the N64 Doom port (`tools/introassets`).

The Makefile converts them with `mksprite -f I8` into `rom:/intro/*.sprite`:
the gray level is used as both intensity and alpha, so black is transparent
and the lines are tinted with the RDP primitive color.
