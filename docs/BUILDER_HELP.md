# Bravely Emerald Builder - Help

This page is part of the program. It needs no browser and no internet connection.

## What the program does

It builds Pokemon Bravely Emerald on this computer from your own Pokemon Emerald ROM file. You choose the ROM and press
**Build Bravely Emerald**. The program does the rest: it checks this computer, installs the build tools it needs, gets
the game files, compiles the game and packs it into a folder.

On Windows it makes a Windows program (32-bit). On Linux and the Steam Deck it makes the Linux (32-bit) version.

## Before you start

* You need an internet connection. The build downloads: the MSYS2 build tools (about 85 MB, Windows only), the compiler
  and library packages (several hundred MB), the SDL2 sources, and the game files from the public pret/pokeemerald
  project. Linux and the Steam Deck download a build container of about 400 MB instead of MSYS2.
* The first build takes about 30 to 60 minutes. Later builds are much faster, because finished steps are kept.
* About 15 GB of free disk space are needed.
* Your ROM is only read to check that it is Pokemon Emerald. It is never copied, changed or uploaded.
* The finished build contains Nintendo's game data. It is for your own use only. Do not share it.

## Add it to Steam (Steam Deck, Game Mode)

The game runs inside the build container, so you add the launcher script, not the game file.

1. In Desktop Mode open Steam, then Games, then Add a Non-Steam Game to My Library.
2. Pick **Pokemon Emerald (Deck)** in the list. If it is not listed, press Browse, choose the file type All files and
   select `emerald-launch.sh` in your install folder. Press Add Selected Programs.
3. Optional: right-click the entry, choose Properties, and rename it or set artwork.
4. Switch back to Game Mode with the Return to Gaming Mode icon. The game is under Non-Steam.

Starting from Game Mode has not been checked by the authors. If it does not start there, start it from Desktop Mode
and report it.

## Saves

Saves are in the `userdata/saves` folder of your install folder. A new build never overwrites an existing save or
your settings.

To move a save between the Steam Deck and Windows, close the game and copy `pokeemerald.sav` from
`userdata/saves` of one install to `userdata/saves` of the other.

## Settings

Open **File, Settings**. There you can change:

* **Install folder.** Where the finished game is put. The default is inside your user folder.
* **CPU usage.** Low (default) uses half of your processor threads at low priority, so the computer stays usable.
  Normal uses three quarters. High uses all of them.
* **Source.** The program carries a copy of the project source. It unpacks it into a work folder and builds there.
  You can change the work folder, re-extract the bundled source, or use your own checkout of the repository instead
  (a checkout of yours is only read, never changed).
* **This computer.** A check of what is installed. It runs by itself at start; it only shows a problem on the main
  window when the problem stops the build.
* On Linux: the name of the build container and the 12 second start-up test.

The log of every build is saved in the `builder-logs` folder (Windows: in the work folder under `build-win`;
Linux: in your install folder). **Settings** shows the exact place.

## If something goes wrong

The program shows a short explanation and a suggested fix. Press **Try again** to continue where it stopped: finished
steps are skipped. **Show details** shows the build log, and **Copy log** puts it on the clipboard. On desktop Linux,
the Builder hides the raw container startup output with the current build script. Running the Bash script directly
leaves Distrobox's startup messages on the terminal.

Common problems:

* **The build container failed for an unknown reason.** If Distrobox uses Podman, run `podman logs --tail 100 emerald-build`
  in a terminal and substitute your container name if different. Inspect the failed attempt locally and remove private
  data before sharing any lines. The Builder does not copy the output of this command into its report.
* **Arch Linux reports an OpenSSL dependency conflict.** The installed packages and the Arch repositories need to be in sync.
* **Distrobox reports `chpasswd: invalid password hash`.** Check `distrobox --version`. Distrobox 1.8.2.5 resolved this
  problem on Linux Mint 22.3.
* **A network operation timed out.** Check your connection to the image registry or package mirror and try again.
* **Podman or its runtime reports a permissions error.** Check your rootless Podman configuration and the reported path.
  On Steam Deck, keep Podman's storage on the internal disk if the error points to external storage.
* **No picture or no display when started over SSH.** Start the program from a terminal on the desktop instead.
* **Not enough disk space.** Free some space or choose another install folder in Settings.
* **A download failed.** Check the internet connection and try again.
* **The ROM was rejected.** Use an unmodified dump of Pokemon Emerald (any language, 16 MiB). Patched, trimmed or
  damaged files do not work.
* **Administrator password.** The program never asks for a password. If a step needs one (installing distrobox or
  podman on another Linux), it shows the exact command to run in a terminal.

## Legal

Build only from your own Pokemon Emerald cartridge dump. The result contains Nintendo's game data: use it yourself and
do not share it. This program and its project hold no ROM and no game data.
