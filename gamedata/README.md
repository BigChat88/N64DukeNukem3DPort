# gamedata/

**Your** original Duke Nukem 3D files go here. They are not included in the repository
(everything here except this README is in `.gitignore`).

```
gamedata/
├── DUKE3D.GRP        base game: shareware 1.3D or Atomic Edition 1.4/1.5
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
- The expansions need the Atomic Edition (1.4/1.5) as the base game.
- Folders whose name starts with `_`, or that have their own `DUKE3D.GRP`, are skipped
  (useful for keeping other versions).
- If an expansion comes as a CD image (`.bin`/`.iso`):
  `python tools/cd_extract.py image.bin gamedata/<name>`.
