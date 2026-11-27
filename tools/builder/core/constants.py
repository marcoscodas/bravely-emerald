"""Facts taken from the repository's own docs and scripts."""
import os
import re
import sys

# --- facts taken from the repository's own docs and scripts ---------------------------------
ROM_SIZE = 16 * 1024 * 1024            # tools/fetch_assets.sh: size 16777216
ROM_TITLE = b"POKEMON EMER"            # offset 0xA0, 12 bytes
EMERALD_US_SHA1 = "f3ae088181bf583e55daf962a92bb46f4f1d07b7"   # tools/fetch_assets.sh EMERALD_US_SHA1
MSYS_PACKAGES = [                       # docs/WINDOWS_BUILD.md, Rebuild step 1
    "make", "git", "cmake", "tar", "patch", "diffutils", "python", "zip", "unzip",
    "mingw-w64-i686-toolchain", "mingw-w64-i686-cmake", "mingw-w64-i686-ninja",
    "mingw-w64-x86_64-gcc", "mingw-w64-x86_64-libpng", "mingw-w64-x86_64-pkgconf",
    "mingw-w64-x86_64-python-pillow",
]
APP_VERSION = "1.0"
MSYS_DEFAULT_ROOT = "C:\\msys64"
SDL2_VERSION = "2.32.10"
SDL2_IMAGE_VERSION = "2.8.12"
# rough download sizes in MB, shown by the plan (the real ones are logged by the build)
DOWNLOAD_MB = {"msys2": 85, "packages": 900, "deps": 12, "assets": 40}
# A dated release with its checksum, not "latest": GitHub's latest became the nightly build, which publishes no checksum.
# The build updates the MSYS2 packages afterwards, so an older installer is fine. To move it, change all three lines; the
# hash is in the release's .sha256 file.
MSYS_INSTALLER_FILE = "msys2-x86_64-20261120.exe"
MSYS_INSTALLER_URL = "https://github.com/msys2/msys2-installer/releases/download/2026-11-20/" + MSYS_INSTALLER_FILE
MSYS_INSTALLER_SHA256 = "8b715fa9e2a667f5c21b34a02d75185eead325370b9a7e5851cf56ae09160370"
MSYS_INSTALLER_HOST_OK = ("https://github.com/msys2/msys2-installer/",)
MIN_FREE_GB = 15
HARD_MIN_FREE_GB = 4

SETTING_KEYS = {"targets": list, "rom": str, "dir": str, "jobs": int, "low_priority": bool,
                "run_test": bool, "container": str, "msys_root": str,
                "own_checkout": str, "classic_font": bool, "source_dir": str, "cpu": str}
CONTAINER_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,40}$")

LEGAL_NOTE = ("Build from your own Pokemon Emerald cartridge dump only. The result contains Nintendo's game data: "
              "use it yourself and do not share it. This repository and this app hold no ROM and no game data.")

REPO_URL = "https://github.com/marcoscodas/bravely-emerald"
