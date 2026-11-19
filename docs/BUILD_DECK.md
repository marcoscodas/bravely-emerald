# Build for the Steam Deck

This makes a native Steam Deck build of the game from **your own Pokémon Emerald dump**. One script
does everything: it creates a build container, builds the libraries and the game, and leaves a
launcher you can add to Steam. It needs no root password and changes nothing on SteamOS itself.

Legal note first: this repository holds no game data and no ROM. The script checks your dump and
downloads the decompilation's game files ([pret/pokeemerald](https://github.com/pret/pokeemerald), through [pokeemerald-multiplatform](https://github.com/gradenGnostic/pokeemerald-multiplatform), each file checked against `tools/assets/manifest.txt`). The build you
get contains Nintendo's data. Use it for yourself and **do not share it**.

## What you need

- A Steam Deck on a recent SteamOS (it ships `distrobox` and `podman`).
- A dump of your own Pokémon Emerald cartridge, any language, as a `.gba` file, copied onto the Deck.
  The script only reads it to check that it is Emerald. It is not copied, stored or uploaded.
- An internet connection (packages, SDL2 sources and the game files are downloaded).
- Free space: about 8 GB on the internal disk (the container lives in `/home`) and 4 GB where you
  put the install folder.
- Time: the first run takes roughly 30 to 60 minutes (the game's compile is the long part). Nothing
  is asked while it runs; plug the Deck in and do not let it sleep.

## Step by step

1. **Desktop Mode.** Press the Steam button, then Power, then *Switch to Desktop*.
2. **Put the ROM somewhere without spaces in the path**, for example `/home/deck/emerald.gba`.
3. **Open Konsole** (the terminal icon in the taskbar) and clone the repository:
   ```sh
   cd ~
   git clone https://github.com/marcoscodas/bravely-emerald bravely-emerald
   cd bravely-emerald
   git checkout deck-port   # only if the port is on that branch
   ```
   (The Deck and Windows ports are on the default branch.)
4. **Run the builder:**
   ```sh
   bash tools/deck/build_deck.sh --rom /home/deck/emerald.gba
   ```
   Options: `--dir <folder>` (where the build, saves and settings go, default `~/emerald-deck`),
   `--jobs N` (compile jobs, default the CPU count up to 4; use `--jobs 2` to keep the Deck cool and
   quiet), `--container NAME` (default `emerald-build`), `--no-run` (skip the final 12 second start-up
   test; the test needs the desktop, so run the script in Konsole and not over SSH).
5. **Read the summary** at the end. It prints the game folder, the launcher and the next steps.
   Running the script again is safe: finished steps are skipped, and it continues where it stopped
   after an error. The ROM is only asked for the first time (afterwards the game files exist).
6. **Play in Desktop Mode:** `~/emerald-deck/emerald-launch.sh`, or the menu entry
   *Pokemon Emerald (Deck)* (Application Launcher > Games).

## Add it to Steam (Game Mode)

The game runs inside the build container, so the thing to add is the launcher script, not the
`pokeemerald` file.

1. In Desktop Mode open **Steam**, then *Games > Add a Non-Steam Game to My Library*.
2. Pick **Pokemon Emerald (Deck)** in the list. (If it is not listed, press *Browse*, choose the
   file type *All files* and select `~/emerald-deck/emerald-launch.sh`.) Press *Add Selected Programs*.
3. Optional: in the library, right-click the entry > *Properties* to rename it or set artwork.
4. Switch back to Game Mode (the *Return to Gaming Mode* icon on the desktop). The game is in
   *Non-Steam* in the library.

The launcher sets `DISPLAY`, `XAUTHORITY` and `XDG_RUNTIME_DIR` itself and starts the game in the
container with `distrobox enter`. If the build container is missing, the launcher exits with an error.
Starting from Game Mode has not been checked by the people who wrote this guide: if it does not
start there, see Troubleshooting and report it.

## Controls

The Deck's controller is read through SDL's game controller layer:

| Deck | Game |
|---|---|
| A / B | A / B |
| X / Y | the Vita's square / triangle actions of the port (shown as X / Y with *BUTTON ICONS* = DECK) |
| L1 / R1 | L / R |
| D-pad or left stick | move |
| Menu (Start) / View (Back) | START / SELECT |

The button prompts in the new menus follow
OPTIONS > GENERAL > INTERFACE > BUTTON ICONS (OFF, GBA or DECK).

## Settings

All port settings are in the START menu under **OPTIONS** (tabs GENERAL, WORLD, BATTLE, MODES, CHEATS).
They are stored in `userdata/settings.txt` as `key=value` lines, so you can also
edit the file with the game closed. Level rows store `0` OFF, `1` AUTO, `2` LOW, `3` MEDIUM, `4` HIGH
(AUTO follows *EFFECTS PRESET*). The full list of keys and menu labels is in the
[settings reference](FEATURES.md#settings-reference). Useful ones:

| Want | Key | Value |
|---|---|---|
| 60 FPS instead of 30 | `voxel_fps` | `1` |
| 2D only, lightest | `voxel_3d` | `0` |
| Old look, no new effects | `voxel_shaders` | `0` |
| Less heat or battery use | `voxel_quality` | `0` (LOW) |
| Original title screen | `voxel_title` | `0` |

`userdata/settings.txt` is created from `tools/deck/settings.txt` the first time. Delete it to go back
to the defaults.

## Saves

Saves are in `~/emerald-deck/userdata/saves/` (`pokeemerald.sav` is the in-game save; the folder is
created on the first save). They are kept **outside** the build folder, so rebuilding or updating
never touches them.

- Back up: copy the `userdata` folder.
- Move to another Deck or to the Windows build: copy `userdata/saves/pokeemerald.sav` to the same
  place of the other install (`userdata\saves\` next to the Windows exe), with both games closed.

## Update

```sh
cd ~/bravely-emerald
git pull
bash tools/deck/build_deck.sh
```

Only what changed is rebuilt, and your saves and settings stay.

## Uninstall

Delete `~/emerald-deck` (or your `--dir`), the clone, `~/.local/share/applications/pokemon-emerald-deck.desktop`,
and the container: `distrobox rm --force emerald-build`.

## Troubleshooting

On desktop Linux, creating a build container requires Distrobox with `--pre-init-hooks`.
The first `distrobox enter` runs `pacman -Syu --noconfirm` inside the new Arch container
before Distrobox installs its basic packages. A failed upgrade stops initialization.
The builder applies the hook only at creation and keeps existing containers.

- **OpenSSL dependency conflict (`libcrypto.so=3-64`) in a container created without the hook.**
  To keep that container, choose an unused name with `--container NAME` and build again.
- **`chpasswd: invalid password hash` during initialization.** Distrobox 1.7.0 supports the hook
  but hit this separate error on Linux Mint 22.3. Distrobox 1.8.2.5 completed initialization
  there. See [issue #6](https://github.com/marcoscodas/bravely-emerald/issues/6).

The [desktop Linux Builder](BUILDER.md#linux-container-diagnostics) shows a short diagnosis of these
failures and keeps the raw startup output out of its report.

On both desktop Linux and SteamOS:

- **A container exists after a failed creation attempt.** The builder stops and keeps the container.
  Inspect it with `podman inspect <name>`, or use an unused name with `--container NAME` to try again.
- **Distrobox did not run the startup check.** Check the container with `distrobox list`.
  If it disappeared, run the builder again to create the Arch container.
- **Distrobox initialization failed.** Check the local log with
  `podman logs --tail 100 emerald-build` (substitute your container name if needed). The builder
  leaves the raw startup logs out of its report. If a package mirror timed out, try again later.
- **The container will not start: `crun: open ... /containers/overlay/.../merged: Permission denied:
  OCI permission denied`.** The container storage was on the SD card. Keep Podman's storage on the
  internal disk (the default; do not add a `storage.conf` pointing at `/run/media`). Remove the broken
  container with `distrobox rm --force <name>` and run the script again.
- **An explicit download timeout, such as `Connection timed out`.** Check your connection to the
  image registry or package mirror and try again.
- **`png.h: No such file` or `libpng not found` while building the host tools.** The libraries were
  still being installed or the install was interrupted. Run the script again; it installs what is missing.
- **`ERROR: Python 3 with Pillow is needed`** from `tools/fetch_assets.sh`. The script installs
  `python-pillow` in the container; if you run `tools/fetch_assets.sh` by hand outside it, install Pillow
  (`pip install pillow`).
- **`tools/fetch_assets.sh` fails at its last step on a clean tree.** Fixed in the current script (it now builds
  the host tools and converts the graphics first). If you use an old copy, run
  `make -f make_tools.mk` and `make -f vita/assets.mk` and then `tools/fetch_assets.sh` again.
- **`Permission denied` when you run `./tools/fetch_assets.sh` or the script.** Run them with `bash`, as in this
  guide.
- **Black window, or the game starts but there is no picture, when started over SSH.** SSH has no X
  authority. SDL then falls back to its offscreen driver. Run from Konsole on the Deck, or export
  `DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/xauth_* | head -1) XDG_RUNTIME_DIR=/run/user/1000` first
  (the launcher does this itself).
- **`Failed to queue audio: Invalid audio device ID`** a few times at start-up. Harmless: calls made before
  the audio device is open. If there is no sound at all, check that the Deck's output is not muted and
  that PipeWire is running (`XDG_RUNTIME_DIR` must point to `/run/user/1000`).
- **The 12 second test says FAILED.** Look at `~/emerald-deck/launch.out`. Start the launcher by hand
  with `DECK_GL_DEBUG=1` (`~/emerald-deck/emerald-launch.sh DECK_GL_DEBUG=1`) and read
  `userdata/gl_errors.log` and `userdata/debug.log`.
- **Out of disk space.** The container needs several GB in `/home`. Free space, or remove an old
  container you no longer use (`distrobox list`, `distrobox rm`).
- **Wrong or damaged ROM.** The script stops at the check (size, title, game code, header checksum). Any
  language of Pokémon Emerald works, other games and bad dumps do not.

## Not checked

Start-up from Game Mode, the power use, and a build on a Deck with an unusual SteamOS channel.
