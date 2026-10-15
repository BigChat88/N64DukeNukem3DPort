# gamedata/

**Your** original Duke Nukem 3D files go here. They are not included in the repository
(everything here except this README is in `.gitignore`).

```
gamedata/
├── DUKE3D.GRP        base game: version 1.4 or 1.5 (the only ones supported)
├── DUKE.RTS          optional (Remote Ridicule)
├── nwinter/          one folder per expansion → output/duke3d-nwinter.z64
│   └── NWINTER.GRP
├── vacation/
│   ├── VACATION.GRP
│   ├── GAME.CON
│   ├── USER.CON
│   └── DEFS.CON
└── dukedc/
    └── DUKEDCPP.SSI
```

- Instead of `DUKE3D.GRP`, a `.zip` of your installation that contains it works too.
- Each expansion folder can hold its GRP, its Sunstorm `.SSI` installer, its loose files or
  a single `.zip` with any of them. The folder name gives the ROM its name
  (`duke3d-<folder>.z64`) and picks the menu background (`assets/addons/<folder>.png`):
  use `nwinter`, `vacation` and `dukedc` to get the ones the project already has.
- Only Duke Nukem 3D 1.4 (the original game updated with the Plutonium PAK) or 1.5 (the
  Atomic Edition, which includes it) is supported, for the base game and as the base of the
  expansions. The packer refuses an older `DUKE3D.GRP` (1.3D or shareware, without
  episode 4).
- Folders whose name starts with `_`, or that have their own `DUKE3D.GRP`, are skipped
  (useful for keeping other versions).
- Instead of its files, an expansion folder can hold its CD image (`.bin` with its `.cue`,
  or `.iso`), also inside a `.zip`: the packer extracts it by itself and takes the folders
  of the CD that have the expansion's GRP or `.SSI` installer (a folder with a
  `DUKE3D.GRP` is a copy of the base game and is left out, as are the CD's programs and
  extras; the audio tracks of a `.cue` are ignored). A Sunstorm CD brings an installer for
  each version of the game (`DUKEDC13.SSI` for 1.3D, `DUKEDCPP.SSI` for 1.4/1.5); only the
  second one is used. A CD with no expansion on it (a compilation of the base game) is
  refused.
