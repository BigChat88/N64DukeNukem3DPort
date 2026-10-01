# gamedata/

Aquí van **tus** archivos originales de Duke Nukem 3D. No se incluyen en el repositorio
(todo lo que hay aquí, salvo este README, está en `.gitignore`).

```
gamedata/
├── DUKE3D.GRP        juego base: shareware 1.3D o Atomic Edition 1.4/1.5
├── DUKE.RTS          opcional (Remote Ridicule)
├── nwinter/          una carpeta por expansión → output/duke3d-nwinter.z64
│   └── NWINTER.GRP
├── vacation/
│   ├── VACATION.GRP
│   ├── GAME.CON
│   ├── USER.CON
│   └── DEFS.CON
└── dukedc/
    └── DUKEDCPP.SSI
```

- En lugar de `DUKE3D.GRP` vale un `.zip` de tu instalación que lo contenga.
- Cada carpeta de expansión puede tener su GRP, su instalador `.SSI` de Sunstorm, sus
  archivos sueltos o un único `.zip` con cualquiera de ellos. El nombre de la carpeta da
  el nombre a la ROM (`duke3d-<carpeta>.z64`) y al fondo del menú
  (`assets/addons/<carpeta>.png`): usa `nwinter`, `vacation` y `dukedc` para aprovechar
  los que ya trae el proyecto.
- Las expansiones necesitan la Atomic Edition (1.4/1.5) como juego base.
- Las carpetas que empiezan por `_`, o que tienen su propio `DUKE3D.GRP`, se ignoran
  (sirven para guardar otras versiones).
- Si una expansión viene en imagen de CD (`.bin`/`.iso`):
  `python tools/cd_extract.py imagen.bin gamedata/<nombre>`.
