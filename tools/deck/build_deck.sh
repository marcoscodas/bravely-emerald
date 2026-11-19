#!/bin/bash
# One-script Steam Deck builder. Run it on the Deck, in Desktop Mode, from a clone of this repository:
#
#     bash tools/deck/build_deck.sh --rom /path/to/your/emerald.gba [--dir <install dir>] [--jobs N] [--no-run]
#
# What it does (every step can be repeated; finished steps are skipped):
#   1. checks the Deck (distrobox, podman, free disk space) and your ROM file;
#   2. creates an Arch Linux distrobox container (default name emerald-build) with the 32-bit toolchain;
#   3. builds static 32-bit SDL2 2.32.10 and SDL2_image 2.8.12 into <dir>/deps32;
#   4. runs tools/fetch_assets.sh (checks your ROM, downloads the game files, converts them);
#   5. builds the game with CMake (-DDECK=ON) into <dir>/build/dist;
#   6. writes a launcher script and a .desktop entry, and starts the game for a short smoke test.
# The ROM is only read by tools/fetch_assets.sh to check that it is Pokemon Emerald. It is not copied or
# stored, and nothing is uploaded. Needs no root: the commands that need root run inside the container.
# See docs/BUILD_DECK.md.
set -eu

SDL_VER=2.32.10
SDL_IMG_VER=2.8.12
# sha256 of the official release tarballs (the same values GitHub publishes for the release assets)
SDL_SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
SDL_IMG_SHA256=393f5efb50536ec13ca4f4affb69cc9966d3c3f969e6c5e701faddf9f9785381
MIN_FREE_HOME_MB=8000
MIN_FREE_DIR_MB=4000
IMAGE=docker.io/library/archlinux:latest

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ROM=""
DIR="$HOME/emerald-deck"
JOBS="$(nproc 2>/dev/null || echo 4)"
[ "$JOBS" -gt 4 ] && JOBS=4
CONTAINER="emerald-build"
RUN=1
FORCE_ASSETS=0

say()  { printf '\n=== %s ===\n' "$*"; }
note() { printf '    %s\n' "$*"; }
die()  { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<USAGE
usage: $0 --rom <emerald.gba> [--dir <install dir>] [--jobs N] [--container NAME] [--no-run]
  --rom        your own Pokemon Emerald dump (.gba). Needed the first time only.
  --dir        where the build, SDL2 libraries, saves and settings live (default: \$HOME/emerald-deck)
  --jobs       parallel compile jobs (default: CPU count, at most 4)
  --container  distrobox container name (default: emerald-build)
  --no-run     skip the 12 second start-up test at the end
  --force-assets  run tools/fetch_assets.sh again even if the game files exist
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --rom) ROM="${2:-}"; shift 2 ;;
        --dir) DIR="${2:-}"; shift 2 ;;
        --jobs) JOBS="${2:-}"; shift 2 ;;
        --container) CONTAINER="${2:-}"; shift 2 ;;
        --no-run) RUN=0; shift ;;
        --force-assets) FORCE_ASSETS=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; die "unknown argument: $1" ;;
    esac
done

# ------------------------------------------------------------------ 1. checks
say "1/6 Checking the Deck"
[ "$(id -u)" -ne 0 ] || die "do not run this as root; run it as the normal user (deck)."
[ "$(uname -m)" = "x86_64" ] || die "this script is for x86_64 Linux (the Steam Deck)."
command -v distrobox >/dev/null || die "distrobox not found. SteamOS ships it; check that you are on a recent SteamOS."
command -v podman >/dev/null || die "podman not found (distrobox needs it)."
case "$JOBS" in ''|*[!0-9]*) die "--jobs needs a number" ;; esac
[ "$JOBS" -ge 1 ] || die "--jobs needs a number of at least 1"
[ -n "$DIR" ] || die "--dir needs a path"
mkdir -p "$DIR" || die "cannot create $DIR"
DIR="$(cd "$DIR" && pwd)"
[ "${REPO#* }" = "$REPO" ] || die "the repository path contains a space; move the clone somewhere without spaces"
[ "${DIR#* }" = "$DIR" ] || die "the install dir contains a space; choose another with --dir"

HAVE_ASSETS=0
if [ "$FORCE_ASSETS" -eq 0 ] && [ -f "$REPO/.assets_ok" ] && [ -f "$REPO/assets/ui2/ui_atlas.bin" ]; then HAVE_ASSETS=1; fi
if [ "$HAVE_ASSETS" -eq 0 ]; then
    [ -n "$ROM" ] || { usage >&2; die "--rom is needed the first time (the game files are not in the repository)."; }
    [ -f "$ROM" ] || die "ROM not found: $ROM"
    [ -r "$ROM" ] || die "ROM not readable: $ROM"
    ROM="$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")"
    [ "${ROM#* }" = "$ROM" ] || die "the ROM path contains a space; use a path without spaces"
    note "ROM: $ROM (only read to check it; it is not copied)"
else
    note "game files already present in the repository: ROM not needed"
fi

free_mb() { df -Pm "$1" | awk 'NR == 2 { print $4 }'; }
FREE_HOME=$(free_mb "$HOME"); FREE_DIR=$(free_mb "$DIR")
note "free space: $FREE_HOME MB in \$HOME (container image and libraries), $FREE_DIR MB in $DIR"
[ "$FREE_HOME" -ge "$MIN_FREE_HOME_MB" ] || note "WARNING: less than $MIN_FREE_HOME_MB MB free in \$HOME; the container may not fit"
[ "$FREE_DIR" -ge "$MIN_FREE_DIR_MB" ] || die "less than $MIN_FREE_DIR_MB MB free in $DIR"
curl -fsS -m 15 -o /dev/null https://github.com || die "no internet (github.com unreachable): the build downloads packages and game files."
note "repository: $REPO"
note "install dir: $DIR, container: $CONTAINER, jobs: $JOBS"

# run a command string inside the container; heavy ones at the lowest priority
enter_container() {
    # Decline the default-image fallback if the named container disappears.
    DBX_NON_INTERACTIVE=0 distrobox enter --name "$CONTAINER" --no-tty -- "$@" <<< n
}
inbox() { enter_container bash -lc "$1"; }
heavy() { enter_container nice -n 19 bash -lc "$1"; }

is_steamos() {
    local os_release=/etc/os-release
    [ -r "$os_release" ] || os_release=/usr/lib/os-release
    [ -r "$os_release" ] && grep -Fxq \
        -e 'ID=steamos' -e 'ID="steamos"' -e "ID='steamos'" \
        -e 'VARIANT_ID=steamdeck' -e 'VARIANT_ID="steamdeck"' -e "VARIANT_ID='steamdeck'" "$os_release"
}

MIN_DISTROBOX=1.8.2.5

# version_older A B: true when dotted version A is older than B; false when A is empty or not a plain version
version_older() {
    local a=$1 b=$2 i x y
    [[ $a =~ ^[0-9]+(\.[0-9]+)*$ ]] || return 1
    local -a av bv
    IFS=. read -ra av <<<"$a"
    IFS=. read -ra bv <<<"$b"
    for i in 0 1 2 3; do
        x=${av[i]:-0}
        y=${bv[i]:-0}
        [ "$((10#$x))" -eq "$((10#$y))" ] || { [ "$((10#$x))" -lt "$((10#$y))" ]; return; }
    done
    return 1
}

list_container_names() {
    local containers
    containers=$(distrobox list) || return
    awk -F'|' 'NR > 1 { gsub(/ /, "", $2); print $2 }' <<<"$containers"
}

# ------------------------------------------------------------------ 2. container
say "2/6 Build container ($CONTAINER)"
container_names=$(list_container_names) || die "could not list Distrobox containers. Check the output above before retrying."
if grep -Fxq -- "$CONTAINER" <<<"$container_names"; then
    note "container exists"
else
    create_args=(--yes --name "$CONTAINER" --image "$IMAGE")
    if ! is_steamos; then
        # Distrobox before 1.8.2.5 cannot set up a user inside a current Arch image (chpasswd: invalid password hash).
        dbx_version=
        dbx_out=$(distrobox --version 2>/dev/null) || true
        [[ $dbx_out =~ ([0-9]+(\.[0-9]+)*) ]] && dbx_version=${BASH_REMATCH[1]}
        if version_older "$dbx_version" "$MIN_DISTROBOX"; then
            die "Distrobox $dbx_version is too old to set up the Arch container (chpasswd: invalid password hash). Install Distrobox $MIN_DISTROBOX or newer, then run the builder again. See docs/BUILD_DECK.md, Troubleshooting."
        fi
        create_help=$(distrobox create --help) || die "could not read Distrobox create options."
        grep -Fq -- '--pre-init-hooks' <<<"$create_help" \
            || die "creating a build container on desktop Linux requires Distrobox with --pre-init-hooks. See docs/BUILD_DECK.md."
        # Exit the container entrypoint on failure, before Distrobox installs basic packages.
        create_args+=(--pre-init-hooks 'pacman -Syu --noconfirm || exit')
    fi
    note "creating it from $IMAGE (downloads about 400 MB)"
    n=0
    while :; do
        create_status=0
        create_output=$(distrobox create "${create_args[@]}") || create_status=$?
        [ -z "$create_output" ] || printf '%s\n' "$create_output"
        # Distrobox 1.x can report success for a name collision, even if list missed it.
        if grep -Fxq -- "Distrobox named '$CONTAINER' already exists." <<<"$create_output"; then
            die "could not create '$CONTAINER': the name is already in use. The container was kept for diagnosis."
        fi
        [ "$create_status" -ne 0 ] || break

        container_names=$(list_container_names) || die "could not list Distrobox containers after create failed. Creation stopped."
        if grep -Fxq -- "$CONTAINER" <<<"$container_names"; then
            die "'$CONTAINER' exists after a failed creation attempt. The container was kept for diagnosis."
        fi
        n=$((n + 1))
        [ "$n" -lt 3 ] || die "distrobox create failed 3 times. See the output above and docs/BUILD_DECK.md, Troubleshooting."
        note "create failed, retrying ($n/3)"
        sleep 5
    done
fi
note "checking that the build container starts${EPOCHREALTIME:+ [$EPOCHREALTIME]}"
# Distrobox can dump raw container logs on failure. Keep them out of Builder reports.
probe_output=$(inbox 'echo container-ok' 2>/dev/null) \
    || die "the container does not start. Distrobox initialization failed for '$CONTAINER'. Inspect its local log with 'podman logs --tail 100 $CONTAINER'.${EPOCHREALTIME:+ [$EPOCHREALTIME]}"
# Declining Distrobox's creation prompt returns zero without running the command.
grep -Fxq -- 'container-ok' <<<"$probe_output" \
    || die "Distrobox did not run the startup check for '$CONTAINER'. Check that the container still exists."
note "build container started"
for p in "$REPO" "$DIR"; do
    inbox "[ -d '$p' ]" || die "$p is not visible inside the container"
done

PKGS="base-devel cmake git pkgconf curl python python-pillow libpng lib32-glibc lib32-gcc-libs lib32-mesa lib32-libglvnd \
lib32-alsa-lib lib32-libpulse lib32-libxcursor lib32-libxi lib32-libxrandr lib32-libxss lib32-libxkbcommon lib32-libpng \
lib32-libxfixes lib32-libxrender lib32-libxtst lib32-pipewire lib32-libdecor wayland-protocols wayland"
if inbox "pacman -Q $PKGS >/dev/null 2>&1"; then
    note "toolchain and libraries already installed"
else
    note "enabling multilib and installing the 32-bit toolchain (a few minutes)"
    # --needed keeps a repeat cheap; the multilib block is added only once
    inbox "sudo bash -c 'grep -q \"^\\[multilib\\]\" /etc/pacman.conf || printf \"\\n[multilib]\\nInclude = /etc/pacman.d/mirrorlist\\n\" >> /etc/pacman.conf; pacman -Syu --noconfirm && pacman -S --needed --noconfirm $PKGS'" \
        || die "package install failed. Run the script again; if it repeats, see docs/BUILD_DECK.md, Troubleshooting."
fi
T32="$DIR/.t32.c"
cat > "$T32" <<'T32SRC'
#include <stdio.h>
int main(void) { printf("%u\n", (unsigned)sizeof(void *)); return 0; }
T32SRC
inbox "gcc -m32 '$T32' -o '$DIR/.t32' && [ \"\$('$DIR/.t32')\" = 4 ]" \
    || die "the 32-bit compiler does not work in the container"
rm -f "${DIR:?}/.t32.c" "${DIR:?}/.t32"
note "32-bit compiler works"

# ------------------------------------------------------------------ 3. SDL2 (32-bit, static)
say "3/6 SDL2 $SDL_VER and SDL2_image $SDL_IMG_VER (32-bit, static)"
P="$DIR/deps32"
if [ -f "$P/lib/libSDL2.a" ] && [ -f "$P/lib/libSDL2_image.a" ]; then
    note "already built in $P"
else
    S="$DIR/build-deps"; mkdir -p "$S" "$P"
    fetch() { # <url> <file> <sha256>
        [ -f "$S/$2" ] || curl -fsSL -o "$S/$2" "$1" || die "download failed: $1"
        got=$(sha256sum "$S/$2" | cut -d' ' -f1)
        note "$2 sha256 $got"
        [ "$got" = "$3" ] || { rm -f "${S:?}/$2"; die "checksum mismatch for $2 (expected $3)"; }
    }
    fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz" "SDL2-$SDL_VER.tar.gz" "$SDL_SHA256"
    fetch "https://github.com/libsdl-org/SDL_image/releases/download/release-$SDL_IMG_VER/SDL2_image-$SDL_IMG_VER.tar.gz" "SDL2_image-$SDL_IMG_VER.tar.gz" "$SDL_IMG_SHA256"
    heavy "bash -c '
set -e
cd $S
rm -rf SDL2-$SDL_VER SDL2_image-$SDL_IMG_VER
tar xf SDL2-$SDL_VER.tar.gz; tar xf SDL2_image-$SDL_IMG_VER.tar.gz
export PKG_CONFIG_PATH=/usr/lib32/pkgconfig:/usr/share/pkgconfig PKG_CONFIG_LIBDIR=/usr/lib32/pkgconfig:/usr/share/pkgconfig
cd SDL2-$SDL_VER
cmake -S . -B build -DCMAKE_C_FLAGS=-m32 -DCMAKE_ASM_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release \
  -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSDL_TESTS=OFF -DCMAKE_INSTALL_PREFIX=$P
cmake --build build -j$JOBS && cmake --install build
cd ../SDL2_image-$SDL_IMG_VER
cmake -S . -B build -DCMAKE_C_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_PREFIX_PATH=$P -DCMAKE_INSTALL_PREFIX=$P -DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_TESTS=OFF \
  -DSDL2IMAGE_DEPS_SHARED=OFF -DSDL2IMAGE_PNG=ON -DSDL2IMAGE_JPG=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF \
  -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JXL=OFF -DSDL2IMAGE_QOI=OFF
cmake --build build -j$JOBS && cmake --install build
'" || die "building SDL2 failed (output above)"
    [ -f "$P/lib/libSDL2.a" ] && [ -f "$P/lib/libSDL2_image.a" ] || die "SDL2 build produced no libraries"
    rm -rf "${S:?}/SDL2-$SDL_VER" "${S:?}/SDL2_image-$SDL_IMG_VER"
fi

# ------------------------------------------------------------------ 4. game files
say "4/6 Game files (tools/fetch_assets.sh)"
if [ "$HAVE_ASSETS" -eq 1 ]; then
    note "already done (.assets_ok present); use --force-assets to redo it"
else
    heavy "cd '$REPO' && FETCH_JOBS=$JOBS bash ./tools/fetch_assets.sh '$ROM'" \
        || die "tools/fetch_assets.sh failed (output above). Not a valid Emerald dump, no network, or a build error."
fi
if [ ! -f "$REPO/assets/ui/intro_margins.bin" ]; then
    note "making the intro margins"
    inbox "cd '$REPO' && python3 tools/ui_gen/gen_intro_margins.py --output assets/ui/intro_margins.bin" \
        || die "intro margins failed"
fi

# ------------------------------------------------------------------ 5. build
say "5/6 Building the game (the first build takes a while, 15 to 40 minutes)"
B="$DIR/build"
CACHE="$B/CMakeCache.txt"
if [ -f "$CACHE" ] && ! grep -q "^CMAKE_HOME_DIRECTORY:INTERNAL=$REPO\$" "$CACHE"; then
    note "stale CMake cache from another checkout, dropping it"
    rm -rf "${B:?}"
fi
mkdir -p "$B/dist" "$DIR/userdata"
# settings and saves live outside the build folder: dist/userdata points here
if [ -d "$B/dist/userdata" ] && [ ! -L "$B/dist/userdata" ]; then
    cp -an "$B/dist/userdata/." "$DIR/userdata/" 2>/dev/null || true
    rm -rf "${B:?}/dist/userdata"
fi
ln -sfn "$DIR/userdata" "$B/dist/userdata"
heavy "cmake -S '$REPO' -B '$B' -DDECK=ON -DDECK_DEPS32='$P' -DCMAKE_BUILD_TYPE=Release" || die "cmake configure failed (output above)"
heavy "cmake --build '$B' -j$JOBS" || die "build failed (output above); run the script again to continue, finished files are kept"
DIST="$B/dist"
{ [ -x "$DIST/pokeemerald" ] && [ -f "$DIST/run.sh" ] && [ -d "$DIST/app0/voxel" ]; } || die "the build finished but $DIST is incomplete"
[ -f "$DIR/userdata/settings.txt" ] || die "userdata/settings.txt missing"
note "game folder: $DIST"

# ------------------------------------------------------------------ 6. launcher
say "6/6 Launcher"
LAUNCH="$DIR/emerald-launch.sh"
cat > "$LAUNCH" <<LAUNCHER
#!/bin/sh
# Starts Pokemon Emerald (Deck build) inside the build container. Extra arguments are KEY=VALUE
# environment settings for the game, e.g. ./emerald-launch.sh DECK_GL_DEBUG=1
container='$CONTAINER'
if ! containers=\$(distrobox list); then
    printf 'ERROR: could not list Distrobox containers. Check that Distrobox and its container engine are available.\n' >&2
    exit 1
fi
if ! printf '%s\n' "\$containers" | awk -F'|' 'NR > 1 { gsub(/ /, "", \$2); print \$2 }' | grep -Fxq -- "\$container"; then
    printf "ERROR: Build container '%s' is missing. Run the builder again.\n" "\$container" >&2
    exit 1
fi
export DISPLAY="\${DISPLAY:-:0}"
if [ -z "\${XAUTHORITY:-}" ]; then XAUTHORITY="\$(ls /run/user/\$(id -u)/xauth_* 2>/dev/null | head -1)"; export XAUTHORITY; fi
export XDG_RUNTIME_DIR="\${XDG_RUNTIME_DIR:-/run/user/\$(id -u)}"
cd '$DIST' || exit 1
DBX_NON_INTERACTIVE=0 exec distrobox enter --name "\$container" --no-tty -- env "\$@" DISPLAY="\$DISPLAY" XAUTHORITY="\$XAUTHORITY" XDG_RUNTIME_DIR="\$XDG_RUNTIME_DIR" ./run.sh <<'DISTROBOX_INPUT'
n
DISTROBOX_INPUT
LAUNCHER
chmod +x "$LAUNCH"
APPS="$HOME/.local/share/applications"
mkdir -p "$APPS"
cat > "$APPS/pokemon-emerald-deck.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Pokemon Emerald (Deck)
Comment=Pokemon Emerald, Steam Deck build made from your own ROM
Exec=$LAUNCH
Path=$DIST
Terminal=false
Categories=Game;
DESKTOP
note "launcher: $LAUNCH"
note "menu entry: $APPS/pokemon-emerald-deck.desktop"

# ------------------------------------------------------------------ smoke test
RESULT="not run (--no-run)"
if [ "$RUN" -eq 1 ]; then
    say "Start-up test (12 seconds)"
    if [ -z "${DISPLAY:-}" ] && [ ! -e /tmp/.X11-unix/X0 ]; then
        RESULT="not run (no display: open Konsole in Desktop Mode, not over SSH)"
    else
        rm -f "$DIST/userdata/gl_errors.log"
        "$LAUNCH" DECK_GL_DEBUG=1 >"$DIR/launch.out" 2>&1 </dev/null &
        sleep 12
        pids=""
        for p in $(ps -eo pid,args | awk '/[.]\/pokeemerald/ { print $1 }'); do
            # inside the container the path shows as /run/host/<path>, so compare the end
            case "$(readlink "/proc/$p/cwd" 2>/dev/null)" in *"$DIST") pids="$pids $p" ;; esac
        done
        if [ -n "$pids" ]; then
            RESULT="OK: the game ran for 12 seconds"
            for p in $pids; do kill "$p" 2>/dev/null || true; done
            sleep 3
            for p in $pids; do kill -9 "$p" 2>/dev/null || true; done
        else
            RESULT="FAILED: the game is not running after 12 seconds, see $DIR/launch.out and docs/BUILD_DECK.md"
        fi
        if [ -f "$DIST/userdata/gl_errors.log" ]; then
            errs=$(grep -c "GLERR" "$DIST/userdata/gl_errors.log" || true)
            RESULT="$RESULT; GL errors logged: ${errs:-0}"
        fi
        rm -f "$DIST/userdata/gl_errors.log" "$DIST/userdata/debug.log"
    fi
fi

cat <<SUMMARY

==========================================================
 Done.
   Game folder : $DIST
   Saves/config: $DIR/userdata  (settings.txt, saves/)
   Launcher    : $LAUNCH
   Start-up test: $RESULT

 Next steps
   - Play now (Desktop Mode): $LAUNCH
   - Game Mode: Steam (Desktop Mode) > Games > Add a Non-Steam Game > "Pokemon Emerald (Deck)"
     (or Browse and pick $LAUNCH), then back to Game Mode. Details: docs/BUILD_DECK.md
   - Do not share the build: it contains Nintendo's game data.
==========================================================
SUMMARY
