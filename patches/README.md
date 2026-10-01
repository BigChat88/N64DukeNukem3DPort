# patches/

Cambios sobre el submódulo `libdragon` (commit fijado en `.gitmodules`/git), aplicados
por GitHub Actions antes de compilarlo. Para compilar desde el código fuente, aplícalos
una vez después de `git submodule update --init`:

```sh
git -C libdragon apply ../patches/libdragon-mixer-clamp-frequency.patch
libdragon install
```

| parche | por qué |
|---|---|
| `libdragon-mixer-clamp-frequency.patch` | el sintetizador MIDI puede pasar un instante de la frecuencia máxima de un canal (nota aguda + envolvente de tono del SoundFont); en vez de abortar con un `assert`, la limita |
