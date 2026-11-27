# RecompLauncher

[RecompLauncher](https://recomplauncher.com) installs recomps from GitHub releases and starts them. This repository holds no
playable game (it would contain Nintendo's data), so what the launcher installs is the **builder**
(`BravelyEmeraldBuilder-win.zip`, `BravelyEmeraldBuilder-linux-x86_64.tar.gz`). The launcher then asks for your Pokémon Emerald ROM,
runs the builder without a window, and starts the game that came out of it.

## The file

`tools/builder/packaging/recomplauncher.json` is copied by `tools/builder/package.py` to the top of both release packages, next to
the program. It uses three levels of the format:

- **Start (level 1).** `game` asks for the ROM. `start.program` lists `game/pokeemerald.exe` (Windows) and `game/emerald-launch.sh`
  (Linux and SteamOS); the first that exists runs. `saves`, `folders` and `actions` point at `game/userdata/`, where
  `saves/pokeemerald.sav`, `settings.txt` and the logs live.
- **Setup step (level 3).** `prepare` runs `BravelyEmeraldBuilder --prepare --game {game} --out {install}/game`. A tick on the setup
  screen ("Download the build tools") adds `--allow-downloads`; it is off until the player ticks it. The step lists
  `{install}/game/prepared.txt` in `done`, so after a successful build the game starts without asking for the ROM again.
  Change `prepare.version` only when players must build again.
- **Settings (level 2)** is not used. See "Open questions".

No ROM, hash of a ROM or game data is in the file. `game.dumps` is left out on purpose: this repository documents the SHA-1 of the
supported ROM (`tools/fetch_assets.sh`), not its CRC32, and the file format wants a CRC32.

## The `--prepare` step

```
BravelyEmeraldBuilder --prepare --game ROM_OR_ZIP --out FOLDER [--allow-downloads] [--target windows|linux32] [--jobs N] [--low]
```

It is the same build as `--build`, without a window. Standard output carries only lines like `progress 0.40 Compiling the game` (the
number comes from the build's stage list, so it is steady rather than exact) and `ERROR: ...` lines. The full log goes to the builder's
usual log file; its path is printed when the build fails. A quiet stage repeats its progress line every 30 seconds.

| Exit code | Meaning |
|---|---|
| 0 | The game is built in `--out`, and `prepared.txt` is written there |
| 1 | The build failed (the log path is printed) |
| 2 | The ROM or an option is wrong (plain message) |
| 3 | The bundled source could not be prepared |
| 4 | Downloads are needed and `--allow-downloads` was not given |

`--game` accepts a `.gba` or a `.zip` that holds one. `--build` also takes `--game` as another name of `--rom`. Without these
arguments the builder behaves as before. The first build downloads about 1 GB of tools and takes 30 to 60 minutes.

## Testing it with the launcher

1. In RecompLauncher open Settings, then Developers, and turn on Developer tools.
2. Choose "Add a build", and pick the folder of an unpacked builder release (the folder that holds `recomplauncher.json`).
3. The Developer tab validates the file and shows what the launcher read from it.
4. Run the setup with your own ROM and watch the progress.

The file can also be checked against the schema at https://recomplauncher.com/schema/recomplauncher.json.

## Folders with spaces on Linux

RecompLauncher's default Linux library is `~/Games/Recomp Library`, so by default `{install}` has a space. A build inside
such a folder fails: `pkg-config` (Arch `pkgconf` 3.0.11 in the build container) splits SDL2's path at the space.

So when the game folder has a space, the builder and `tools/deck/build_deck.sh` put SDL2, the build and the game files in
`bravely-emerald-parts-<8 characters>`, above the first space when that is writable and on the same drive, otherwise
`~/emerald-deck-parts-<8 characters>`. The characters come from the game folder's path, so two installs never share a build.
The launcher script, saves and settings stay in `{install}/game/`, so Easy Setup starts the game as usual. The setup step's
progress text and the builder window name both folders.

Limits: the guide asks a game to write only under `{install}` and `{data}`, and this folder is outside them. Moving,
backing up or uninstalling the game in the launcher does not move or remove it, and the launcher script holds full paths:
do not move the game or that folder after building. `--parts-dir <folder without spaces>` picks the folder by hand.

## Open questions

- Whether the launcher accepts a single `.gba` file for `game.kind: "iso"` (the schema names only `iso`, `folder`, `iso_or_folder`).
- Whether `userdata/settings.txt` (`key=value` lines with no section) can be a `config` of format `ini`. Until that is known the
  settings stay in the game's own menu.
