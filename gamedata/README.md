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
- An expansion folder can also hold its CD image (`.bin` with its `.cue`, or `.iso`): the
  packer extracts it by itself. A Sunstorm CD brings an installer for each version of the
  game (`DUKEDC13.SSI` for 1.3D, `DUKEDCPP.SSI` for 1.4/1.5); only the second one is used.
