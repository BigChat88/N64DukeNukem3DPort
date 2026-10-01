# patches/

Changes to the `libdragon` submodule (commit pinned in `.gitmodules`/git), applied by
GitHub Actions before building it. To build from source, apply them once after
`git submodule update --init`:

```sh
git -C libdragon apply ../patches/libdragon-mixer-clamp-frequency.patch
libdragon install
```

| patch | why |
|---|---|
| `libdragon-mixer-clamp-frequency.patch` | the MIDI synthesizer can briefly go over a channel's maximum frequency (high note + SoundFont pitch envelope); instead of aborting with an `assert`, it clamps it |
