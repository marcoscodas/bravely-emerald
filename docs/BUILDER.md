# The builder app

A small desktop program (Python 3 and Qt through PySide6, in the look of Windows 95) that builds the game from
**your own Pokemon Emerald dump**, for Windows or for Linux and the Steam Deck. You choose the ROM and press one button.
The program carries the project source inside it, checks the computer, installs what is missing, gets the game files,
compiles and packs the game. It runs the existing build scripts (`tools/deck/build_deck.sh`, `tools/fetch_assets.sh` and
`tools/windows/*`) as subprocesses; it does not replace them.

![The main screen](screens/builder_idle.png)

Legal note first: the repository holds no ROM and no game data, and neither does the app. The result of a build
contains Nintendo's data. Use it yourself and **do not share it**. The ROM is only read to check it (by
`tools/fetch_assets.sh`); it is never copied, stored or uploaded.

## Standalone for the repository, not for the toolchain

The package contains everything of this repository that a build needs, so the only thing you supply is the ROM. It
does **not** contain the toolchain or the game files. During the build, and only after you confirm it once, the
program downloads automatically:

* Windows: the MSYS2 installer (about 85 MB, official, from github.com/msys2) and the compiler and library packages
  (several hundred MB) if they are not installed yet. Linux and Steam Deck: an Arch Linux build container (about
  400 MB) and its compiler packages.
* The SDL2 2.32.10 and SDL2_image 2.8.12 sources (a few MB, checked against SHA-256 values on Linux).
* The game files from the public pret/pokeemerald decompilation (through pokeemerald-multiplatform), each file checked
  against `tools/assets/manifest.txt`.

So you need an internet connection, about 15 GB of free disk space and, the first time, 30 to 60 minutes.

## Install and run

**Windows.** Download `BravelyEmeraldBuilder-win.zip`, unzip it anywhere, run `BravelyEmeraldBuilder.exe`. No Python is
needed. (There is no installer yet; an Inno Setup or NSIS installer can be added later.)

**Linux, SteamOS.** Unpack `BravelyEmeraldBuilder-linux-x86_64.tar.gz`, then `sh install.sh` (copies it to
`~/.local/share/bravely-emerald-builder` and adds a menu entry; `sh install.sh --uninstall` removes it). On a Deck use
Desktop Mode.

**From source.** With the repository cloned: `pip install PySide6` (PySide6-Essentials is enough), then
`python tools/builder/app.py`. A source checkout is used as the repository, unless a package archive was made next to
the app (`tools/builder/bundle/`, written by `package.py`); then that snapshot is used, as in the packaged app.
`--repo DIR` always wins. Other options: `--dry-run` (the log shows every command, nothing runs), `--smoke` (open the
window and quit, a self test), `--selftest` (extract the bundled source and dry-run this host's build, no window),
`--build --rom FILE --out DIR [--jobs N] [--low]` (build without a window; for tests).

## The screens

The window has the layout of the mockup: a Windows 95 window (480 x 360 pixels at 1x, fixed size, like a normal Windows 95 dialog) with its own title bar, a `?` button
(opens the Help page) and an `X` button (close). Esc and Alt+F4 close it too; double-clicking the title bar does nothing.

![Main screen, 2x](screens/builder_idle.png)

1. **Select ROM location**: a sunken text field and **Browse**. The result is shown under it: "Pokemon Emerald found (USA)." or a
   short reason (wrong size, not Emerald, modified header). The ROM is only needed the first time.
2. **Where to build?**: the folder for the finished game (default `Documents\Bravely Emerald` on Windows, `~/emerald-deck` on Linux).
3. The consent checkbox: "Yeah, download the nerdy stuff needed to build (requires internet and it is safe). \*". The link under
   it opens a message box with what is downloaded and the approximate sizes (MSYS2 or the build container, compiler packages, SDL2,
   the game files from the public pret/pokeemerald project), the 30 to 60 minutes, the 15 GB, that the ROM is only read and never copied
   or uploaded, and that the build is for your own use only. The checkbox is the consent: there is no extra dialog.
4. **Build** (default button). It stays disabled until a valid ROM (or the game files of an earlier build), a valid folder and the
   ticked checkbox are there. A **Settings...** link sits at the bottom left.

![Ready to build](screens/builder_ready_to_build.png)

The target follows the computer: a Windows exe on Windows, the Linux 32-bit (Steam Deck) build on Linux and SteamOS. The prerequisite
check runs by itself in the background at start and only shows a problem on the form when it stops the build. Then everything runs
without more decisions: check the computer, install the build tools, get the game files, compile, package.

![Building, details shown](screens/builder_building.png)

**While building:** the form is replaced by the progress view: the classic blue-block bar, the step in plain words, the elapsed time,
**Cancel** (stops the whole process tree) and **Show details >>** for the live log (a sunken white field, monospace). **Copy log**
copies it. A failure shows a short explanation and the suggested fix from the table of known errors, in the window and in a classic
message box with **Retry**; **Try again** resumes (finished steps are skipped).

![A failed step](screens/builder_failed.png)

**Done:** "Your build is ready" with the folder, **Open folder**, **Play**, and one line of next step (Windows: press Play or
double-click `run.bat`; Linux and Deck: add the launcher to Steam, with a Help button).

![Done](screens/builder_done.png)

**Settings...** (CPU usage Low/Normal/High, Low is the default: half of the threads at low priority; the source: work folder, *Use my
own checkout*, *Re-extract bundled source*; the Linux container name and start-up test; a button that lists the result of the computer
check; the place of the build logs) and **?** (the Help page, `docs/BUILDER_HELP.md`, bundled in the app and rendered inside it, no
browser, no network: Steam Game Mode steps, saves, troubleshooting, legal text) hold everything else.

Choices are remembered in `bravely_builder_settings.json` next to the app (or in `~/.config/bravely-builder` when that folder is not
writable): paths and option values only, never ROM contents.

## The bundled source

`package.py` runs `git archive` on the current commit (tracked files only: no `.git`, no virtualenv, no builds, no game
data) and drops what no build needs: `docs/` except `docs/licenses/`, `design/` except `design/stitch/_tools/` (the
design exporter that `tools/fetch_assets.sh` runs), `tools/builder/`, `.github/`, README and
CONTRIBUTING. The result is `bundle/source.tar.xz` plus `source-manifest.json` (commit, date, file count, sha256 of the
archive, the exclude list), stored inside the app. The list was proven by a real build from the package.

On first run, or when the bundled version differs from the extracted one, the app unpacks it into a work folder
(`%LOCALAPPDATA%\BravelyEmerald\source`, `~/.local/share/bravely-emerald/source`; `BRAVELY_BUILDER_HOME` moves it for tests) and
builds there; the build output goes to the separate install folder. Extraction checks the archive hash against the
manifest first, refuses absolute paths, `..`, drive letters, links and special files, goes through a staging folder that is
renamed into place, and an update only rewrites files whose content changed, so `build-win/deps`, `.assets_ok` and the
downloaded game files survive and a second build is fast. A marker file says when an extraction is incomplete, and an
interrupted one is repeated. A folder that the app did not create is never overwritten. A checkout of your own (Settings) is only read.

## What it runs

| Host | Commands |
|---|---|
| Windows | If missing: the MSYS2 installer (silent, `--confirm-command --accept-messages --root`) and `pacman -Syu`, `pacman -S --needed` of the packages in `docs/WINDOWS_BUILD.md`. Then, in MSYS2 (`C:\msys64`): `tools/fetch_assets.sh` (`FETCH_NO_CONVERT=1`, accepted to stop at "Generating the port's files"), `make -f make_tools.mk`, `make -f vita/assets.mk`, `tools/fetch_assets.sh` again, `tools/windows/build.sh`, `tools/windows/package.sh`, and a copy of `dist-win` to the install folder that never overwrites saves or settings. |
| Linux, SteamOS | `bash tools/deck/build_deck.sh --dir D --jobs N --container C [--rom R] [--no-run]`: the script makes the container, the toolchain, SDL2 and the game. A missing `distrobox` or `podman` stops the build with the exact command to run in a terminal. |

The job count of the Windows build reaches `build.sh` through `OMP_NUM_THREADS` (GNU `nproc` honours it). Low priority is the
Windows `BELOW_NORMAL` priority class or `nice -n 19`.

## Security model

* **No passwords.** The program never asks for, reads or stores a password; if something needs administrator rights it shows
  the command to run in a terminal.
* **No shell strings.** Commands are argument lists. For MSYS2 the script text is constant and every path is passed as a
  positional argument, so a path cannot inject commands. Paths from the window are validated: absolute, no control
  characters, no quotes or shell symbols, no spaces for the paths the scripts use.
* **Downloads need consent**, given once in the confirmation box. The program itself downloads only the MSYS2 installer; the
  rest is downloaded by the build scripts.
* Settings are whitelisted by key and type. The program listens on no port.

## Layout and extending

```
tools/builder/
  app.py            entry point (Qt)           cli.py   headless driver of the core
  core/             no Qt: tested without a display
    host.py  constants.py  validate.py  errors.py  settings.py  tasks.py  repo.py
    builder.py      targets, checks, command construction, runner, installs, results
    source.py       bundled source: manifest, safe extraction, update, which repository is used
  ui/               win95.py (the Windows 95 style, colours, pixel glyphs, font), main_window.py (the window), dialogs.py (Settings, Help)
  fonts/            W95FA.otf and its licence
  tests/            test_core.py, test_source.py, test_ui.py (offscreen), shots.py (renders every screen to PNG)
  package.py        bundle + PyInstaller        packaging/  Linux install files, container build script
docs/BUILDER_HELP.md   the Help page (bundled into the app)
```

* A new known error: add a `(regex, title, fix)` row to `core/errors.py` and a case to `ErrorTableTests`.
* A new prerequisite: add it in `Builder._checks_windows` or `_checks_linux`.
* A new stage: add a `(regex, stage)` rule to the `Step` and the words in `STEP_TEXT` in `ui/main_window.py`.
* Tests: `python -m unittest discover -s tools/builder/tests -v` (the window tests need PySide6; on Windows with the offscreen
  platform set `QT_QPA_FONTDIR=C:\Windows\Fonts`). `BRAVELY_TEST_ROM` adds a check against a real dump.
  `BRAVELY_FAKE_HOST=steamos` (with `--dry-run`) shows the Linux command construction on any machine.
* Screens: `python tools/builder/tests/shots.py OUTDIR`.

## Look

A hand written Windows 95 style (`ui/win95.py`: a `QProxyStyle` over Qt's `Windows` style, no style sheet) with the colours of the
**Windows 95 "Windows Standard" scheme**. The values come from the "Windows Classic" column of
[brethorsting.com/blog/2007/12/windows-system-colors](https://www.brethorsting.com/blog/2007/12/windows-system-colors/); Wine's defaults
and the "Windows Standard" column of that page are the later Windows 2000 / XP scheme (face `#D4D0C8`) and were not used.

| Element | Colour |
|---|---|
| Face (windows, buttons) | `#C0C0C0` |
| 3D highlight (ButtonHilight) | `#FFFFFF` |
| 3D light (ButtonLight) | `#C0C0C0` (the same as the face in Windows 95) |
| 3D shadow (ButtonShadow) | `#808080` |
| 3D dark shadow | `#000000` |
| Text, window (text fields) | `#000000`, `#FFFFFF` |
| Active caption, selection | `#000080` with `#FFFFFF` text (inactive caption `#808080`, text `#C0C0C0`, not used: the window is always active) |
| Tooltip | `#FFFFE1` |
| Disabled text (GrayText) | `#808080` |

The Win95 scheme has no `#DFDFDF`: in Windows 95 the "light" edge colour is `#C0C0C0`, so the inner top-left line of a raised edge
is invisible against the face. (`#DFDFDF` appears in later themes.)

Edges are built like GDI `DrawEdge`, two pixels wide: raised = outer top-left highlight, outer bottom-right dark shadow, inner
top-left light, inner bottom-right shadow; sunken (text fields, the progress trough, the log) = the reverse, with the dark shadow
inside at the top left; pressed buttons are the raised edge inverted; the default button has an extra black 1 px outline; the focus
is a dotted rectangle; the etched line is 1 px `#808080` over 1 px white; the window border is 2 px (light and white at the top
left, black and `#808080` at the bottom right). Check boxes are 13 x 13 sunken white squares with a hand drawn black check. The
progress bar is a sunken trough with navy `#000080` blocks 8 px wide and 2 px apart. The caption is 20 px high (Win95 uses 18; the
16 px font needs the room), with the 16 x 16 gem icon, the title in bold white and two 16 x 14 raised buttons whose `?` and `X`
glyphs are pixel bitmaps in `win95.py`, not font characters. There are no animations and no sounds. `tests/test_ui.py` checks the
pixels of the edges.

![Zoom: the default button](screens/builder_zoom_button.png)
![Zoom: the progress bar](screens/builder_zoom_progress.png)

**Font.** The text is a scalable, antialiased font in point sizes, so it is crisp at every size: 9 pt for the text, 10 pt bold for the
section labels, 9 pt bold in the caption, 9 pt Consolas / Courier New in the log. On Windows the first installed of Microsoft Sans
Serif (the TrueType successor of Windows 95's MS Sans Serif), Tahoma, Segoe UI, Arial; elsewhere Liberation Sans, DejaVu Sans, Noto
Sans (`win95.pick_family`, tested). Nothing new is bundled for this. The Settings, Help and message boxes use the same font. The
pixel font **W95FA** (`tools/builder/fonts/W95FA.otf`, Alina Sava, SIL Open Font License 1.1, from
https://fontsarena.com/wp-content/uploads/2020/01/W95FA.zip, licence in `docs/licenses/W95FA-OFL.txt`) is only an option:
Settings > Text > "Classic pixel font" (default off; needs a restart) or `BRAVELY_FONT=classic`. It is drawn at 16 px with no
antialiasing and the UI scale is then rounded down to a whole number, because a pixel font is only sharp at whole multiples of its
grid. `BRAVELY_FONT=system` forces the normal font.

**Edges in device pixels.** Bevel lines, the etched line and the pixel glyphs (check mark, ? and X) are drawn in device pixels
(`win95._device_rect` maps the logical rectangle through the painter transform and rounds the edges), so every line is exactly one
device pixel wide at 100%, 125%, 150% and so on, never a blurred 1.25 px line. The icons are drawn at the device resolution.

**Size and scaling.** The window is designed at 1x (**480 x 360 logical pixels**) and enlarged by a scale that can be fractional,
because the text is vector. The rule (`core/scale.py`, tested): the *target* is the scale that makes the window about 42% of the
screen height (the middle of 40 to 45%), in steps of 0.25 and never below 1.0; the *OS scale* (Windows 125%, 150%, a 2x display) is
used when it is larger than the target; the *cap* is the largest step that keeps the window within 55% of the screen width and
height; scale = max(OS, target), limited by the cap. The OS scale is not multiplied again: Qt applies it (rounding policy
PassThrough, so 125% stays 125% and is not rounded to 100% or 200%) and the app adds only the difference through `QT_SCALE_FACTOR`.
The screen is found by a short child process before Qt starts (`--probe-screen` in the packaged exe). `BRAVELY_UI_SCALE=1.25`
(decimals) overrides the choice. The chosen scale and window size are the first line of the details log.

| Screen | OS scale | UI scale | Window | % of width / height |
|---|---|---|---|---|
| 1280 x 800 (Steam Deck) | 100% | 1.0 | 480 x 360 | 38 / 45 |
| 1366 x 768 | 100% | 1.0 | 480 x 360 | 35 / 47 |
| 1920 x 1080 | 100% | 1.25 | 600 x 450 | 31 / 42 |
| 2560 x 1080 (ultrawide) | 100% | 1.25 | 600 x 450 | 23 / 42 |
| 1920 x 1080 | 150% | 1.5 | 720 x 540 | 38 / 50 |
| 2560 x 1440 | 100% | 1.75 | 840 x 630 | 33 / 44 |
| 3840 x 2160 | 100% | 2.5 | 1200 x 900 | 31 / 42 |

Screenshots of the text at 125% and 200%, and a 4x zoom of the letters (smooth antialiasing, no staircase pixels):

![The window at 125%](screens/builder-text-125.png)
![The window at 200%](screens/builder-text-200.png)
![Zoom 4x of the text](screens/builder-text-zoom4x.png)

**Frame.** The window is frameless with the title bar drawn by the app. Dragging uses `QWindow.startSystemMove()`; if the platform
does not support it the app moves the window by mouse and says so in the details log; on Wayland without move support it falls back to
the native frame, also logged. `BRAVELY_NATIVE_FRAME=1` forces the native frame. The Settings and Help dialogs and the message boxes
use the native frame of the system with the same style inside.

## Packaging

PyInstaller has to run on the OS it targets.

```sh
python -m venv .venv-builder
.venv-builder/bin/pip install PySide6-Essentials pyinstaller      # Scripts\pip on Windows
.venv-builder/bin/python tools/builder/package.py
```

Windows makes `tools/builder/dist-builder/BravelyEmeraldBuilder-win.zip`. Linux makes the `.tar.gz` with `install.sh` and the
`.desktop` template. On a Steam Deck run `bash tools/builder/packaging/build_linux_in_container.sh`: it creates a fresh container
(never the game's `emerald-build`), installs Python and git in it, runs `package.py` and removes the container.

## Relation to the Emerald 3DS web builder

The [Emerald 3DS builder](https://github.com/ZallaxDev/emerald-3ds/tree/main/builder) runs in the browser (Pyodide) because their
game executable is prebuilt and independent of the game data: the builder only packs the data from the ROM into a `.pak`. Our
builds compile the decompiled game with Nintendo's graphics and data linked into the executable, so a prebuilt downloadable
executable would distribute game data, and a 32-bit native compiler cannot run in a browser. A browser-only builder is therefore not
possible here and its code is not reused (it is also under CC BY-NC-SA 4.0). This app runs on your computer and nothing leaves it.

Packaging collects `core/` and `ui/` with `--collect-submodules` (never a hand-written list), writes the module list to `bundle/modules.txt`, and runs the frozen app with `--imports` (imports every module and prints OK or FAIL for each); a package with a missing module is not produced. The test `test_fresh_smoke.py` also starts the exe with no arguments, like a double-click, and looks for its window.

## How to check a fresh install without installing anything

The builder looks at what is already on the PC before it does anything (`core/probe.py`), so it installs, downloads or builds only
what is missing or outdated, and it prints a visible "already installed / already done" line for everything it skips.
`plan` shows this without changing anything: one row per component (MSYS2, MSYS2 packages, SDL2 + SDL2_image, host tools, game
files, CMake build folder; on Linux podman, distrobox, container, deps32, game files, build folder) as PRESENT, MISSING or OUTDATED,
the size to download, and what a build would do.

    BravelyEmeraldBuilder.exe --plan                 (or: python tools/builder/cli.py plan [--target windows|linux32] [--json])
    BravelyEmeraldBuilder.exe --build --dry-run --rom X.gba --out DIR     (prints the commands, runs nothing)
    set BRAVELY_MSYS2_ROOT=C:\empty-folder           (pretend MSYS2 is not installed: the plan then lists the MSYS2 and package steps)

One command runs the smoke tests (no network, no installs, about two minutes):

    python -I -m unittest discover -s tools/builder/tests -t . -p test_fresh_smoke.py

It runs the real core code against a stub `pacman` (first sync that asks for a restart, mirror timeouts and retries, lock file,
keyring error, resume), runs the real `build.sh`, `build_deps.sh` and `package.sh` with stand-in tools (`BRAVELY_STUBS`) from an empty
folder, a folder with spaces, one with an accent and a `Users\Player\AppData\Local\BravelyEmerald\source` style path, and runs `plan`
and `build --dry-run` against this PC and against an empty MSYS2 folder, on the source and on the packaged exe when
`tools/builder/dist-builder/` holds one (or `BRAVELY_PACKAGED_EXE` names it). A failed check prints which step and which line.
It does not prove that a real MSYS2 install, the real mirrors or the real compile work on a clean PC: only a real run on one does.

When something fails, the app's "Copy log" button copies the build log for a GitHub issue. It includes the app version, bundled
source commit, system, CPU, RAM, disk, the plan table, and a footer with the failed step, exit code and probable cause. On Windows
it also includes MSYS2 and `pacman -Q` versions. On desktop Linux it records the Distrobox and Podman versions when available.

An error inside the app itself is written to `crash.log` in the app folder and shown in a box with Copy log.

### Linux container diagnostics

On desktop Linux, the graphical Builder hides raw output from the first `distrobox enter` in **Show details**,
**Copy log** and the saved build log when using the updated build script. The Builder CLI uses the same filter.
Running `bash tools/deck/build_deck.sh` directly leaves Distrobox's startup messages on the terminal. Some Distrobox
versions include historical logs in those messages, so review them before sharing.

If the first `enter` fails, the Builder looks up the container ID through Distrobox (up to two seconds), then asks
Podman for up to 100 timestamped lines from that ID during the `enter` interval (up to three seconds). It uses only
records whose timestamps fall within that interval and reports a short diagnosis. The time window filters out earlier
attempts, but a concurrent process writing to the same container during the interval can still affect the diagnosis.
The lookup depends on Podman and Distrobox agreeing on the container ID and on comparable timestamps.

If the cause is unknown and Distrobox uses Podman, run `podman logs --tail 100 emerald-build` in a terminal, using
the build's container name if different. This local command can show records from earlier attempts. Review the lines
locally for private data before sharing any of them. See [the build guide](BUILD_DECK.md#troubleshooting).

The Linux diagnostics integration test runs the real Bash script with local Distrobox and Podman stand-ins. It needs
Bash and basic Linux utilities and runs without a ROM, network access, containers or administrator privileges.

    python -m unittest -v tools.builder.tests.test_linux_diagnostics

## Tested and not verified

Tested on Windows 10 (2026-11-15): 66 core and window tests; every screen rendered offscreen at 1x and 2x and read (with 4x zooms of the title bar, buttons, field, checkbox and progress bar); simulated desktops of 1280x800 to 3840x2160; the packaged exe (`--smoke`, `--selftest`); and builds through the packaged exe whose repository is only its bundled snapshot, from a folder with no repository: a full build (29 min 22 s), a second full build of the newer bundle onto the same work folder (4 min 52 s), and a rebuild after the `build.sh` cache fix (1 min 47 s, nothing recompiled). The game started, showed the title screen and logged no GL error each time.

`tools/windows/build.sh` had a bug that deleted `build-win/out` on every run (the cache check compared an MSYS path with a Windows path), so every Windows rebuild compiled everything; it is fixed in the same change.

Not verified: the Linux and Steam Deck target beyond command construction (the Deck was not reachable), the Linux package and `build_linux_in_container.sh`, the MSYS2 install and the package install on a computer without MSYS2, the window frame drag on Linux and Wayland (only the fallback logic is tested), and Game Mode start-up (as in `BUILD_DECK.md`).
