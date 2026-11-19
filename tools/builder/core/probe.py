"""What is already on this machine. Every installable component is probed first, so the build installs, downloads or
rebuilds only what is missing or outdated. The probes are plain methods so tests can replace them with a fake host."""
import glob
import os
import re
import subprocess
from datetime import datetime, timezone

from .constants import CONTAINER_RE, MSYS_PACKAGES, SDL2_VERSION, SDL2_IMAGE_VERSION, MSYS_DEFAULT_ROOT
from .errors import ANSI_ESCAPE


def linux_tool_version(tool):
    """Read only the version field, since a tool can also print warnings."""
    try:
        p = subprocess.run([tool, "--version"], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL, encoding="utf-8", errors="replace", timeout=2)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if p.returncode != 0:
        return None
    version = re.search(r"^(?:" + re.escape(tool) + r"(?: version)?[: \t]+)?v?"
                        r"(\d+\.\d+(?:\.\d+){0,2}(?:[~+.-][\da-zA-Z][\da-zA-Z.+~-]*)?)[ \t]*$",
                        p.stdout, re.M)
    return version.group(1) if version else None


DISTROBOX_ID = re.compile(r"[0-9a-f]{12,64}")
PODMAN_TIMESTAMP = re.compile(r"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)(?:\.(\d{1,9}))?(Z|[+-]\d\d:\d\d)")


def podman_logs(container, started, finished):
    """Read timestamped Podman records for this Distrobox container during the first enter."""
    if not CONTAINER_RE.fullmatch(container) or started >= finished:
        return []
    since = datetime.fromtimestamp(started, timezone.utc).isoformat()
    until = datetime.fromtimestamp(finished, timezone.utc).isoformat()
    try:
        listing = subprocess.run(["distrobox", "list"], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL, encoding="utf-8", errors="replace", timeout=2)
        if listing.returncode != 0:
            return []
        rows = [row.split("|", 2) for row in ANSI_ESCAPE.sub("", listing.stdout).splitlines()]
        ids = [row[0].strip() for row in rows if len(row) == 3 and row[1].strip() == container]
        if len(ids) != 1 or not DISTROBOX_ID.fullmatch(ids[0]):
            return []
        # Use Distrobox's ID instead of opening a different backend's container with the same name.
        p = subprocess.run(["podman", "logs", "--since", since, "--until", until, "--timestamps",
                            "--tail", "100", "--", ids[0]],
                           stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           encoding="utf-8", errors="replace", timeout=3)
    except (OSError, subprocess.TimeoutExpired):
        return []
    if p.returncode != 0:
        return []
    messages = []
    # Recheck timestamps to exclude out-of-window records and unstructured tool diagnostics.
    for line in p.stdout.splitlines()[-100:]:
        timestamp, _, message = line.partition(" ")
        match = PODMAN_TIMESTAMP.fullmatch(timestamp)
        if not match:
            continue
        date, fraction, offset = match.groups()
        micros = (fraction or "").ljust(6, "0")[:6]
        try:
            when = datetime.fromisoformat(date + "." + micros + offset.replace("Z", "+00:00")).timestamp()
        except ValueError:
            continue
        if started <= when < finished:
            messages.append(message)
    return messages


def find_msys_root(setting=None, env=None):
    """(root, how) of an existing MSYS2, else (default install root, None). Order: BRAVELY_MSYS2_ROOT, the saved setting,
    C:\msys64, msys64 on any other drive, the uninstall registry key."""
    env = os.environ if env is None else env
    forced = env.get("BRAVELY_MSYS2_ROOT")
    if forced:
        return forced, ("BRAVELY_MSYS2_ROOT" if os.path.isfile(os.path.join(forced, "usr", "bin", "bash.exe")) else None)
    cands = []
    if setting:
        cands.append((setting, "saved setting"))
    cands.append((MSYS_DEFAULT_ROOT, "default folder"))
    for letter in "DEFGHIJKLMNOPQRSTUVWXYZ":
        cands.append((letter + ":\msys64", "msys64 on drive " + letter))
    for path, how in cands:
        if os.path.isfile(os.path.join(path, "usr", "bin", "bash.exe")):
            return path, how
    reg = _registry_root()
    if reg and os.path.isfile(os.path.join(reg, "usr", "bin", "bash.exe")):
        return reg, "uninstall registry key"
    return setting or MSYS_DEFAULT_ROOT, None


def _registry_root():
    try:
        import winreg
    except ImportError:
        return None
    for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
        for sub in (r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall", r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall"):
            try:
                with winreg.OpenKey(hive, sub) as k:
                    for i in range(winreg.QueryInfoKey(k)[0]):
                        name = winreg.EnumKey(k, i)
                        if "msys2" not in name.lower():
                            continue
                        with winreg.OpenKey(k, name) as sk:
                            try:
                                return winreg.QueryValueEx(sk, "InstallLocation")[0]
                            except OSError:
                                continue
            except OSError:
                continue
    return None


def pc_version(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            m = re.search(r"^Version:\s*(\S+)", f.read(), re.M)
        return m.group(1) if m else None
    except OSError:
        return None


def deps_state(repo):
    """present | missing | outdated for build-win/deps (mirrors deps_state in tools/windows/lib_cache.sh)."""
    d = os.path.join(repo, "build-win", "deps")
    libs = os.path.join(d, "lib")
    have = [os.path.isfile(os.path.join(libs, n)) for n in ("libSDL2.a", "libSDL2_image.a")]
    if not all(have):
        return "outdated" if any(have) else "missing"       # one library only: a half-built prefix
    pc = os.path.join(libs, "pkgconfig")
    ok = (pc_version(os.path.join(pc, "sdl2.pc")) == SDL2_VERSION and pc_version(os.path.join(pc, "SDL2_image.pc")) == SDL2_IMAGE_VERSION)
    return "present" if ok else "outdated"


def host_tools_present(repo):
    return all(os.path.isfile(os.path.join(repo, "tools", t, t + ".exe")) for t in ("preproc", "gbagfx"))


def build_dir_state(repo):
    """present (a cache for this folder) | missing | outdated (cache of another folder: it will be dropped)."""
    cache = os.path.join(repo, "build-win", "out", "CMakeCache.txt")
    if not os.path.isfile(cache):
        return "missing"
    try:
        with open(cache, encoding="utf-8", errors="replace") as f:
            m = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", f.read(), re.M)
    except OSError:
        return "outdated"
    home = (m.group(1) if m else "").replace("\\", "/").rstrip("/").lower()
    here = os.path.abspath(repo).replace("\\", "/").rstrip("/").lower()
    return "present" if home == here else "outdated"


class Probe:
    """The real probes. A test fake only has to provide the same methods."""

    def __init__(self, builder):
        self.b = builder

    def msys(self):
        return self.b.msys_root_found()

    def packages_missing(self):
        return self.b._missing_packages()

    def deps(self):
        return deps_state(self.b.repo)

    def tools(self):
        return host_tools_present(self.b.repo)

    def assets(self):
        return self.b.have_assets()

    def build_dir(self):
        return build_dir_state(self.b.repo)

    def exe_built(self):
        return os.path.isfile(os.path.join(self.b.repo, "build-win", "out", "dist", "pokeemerald.exe"))

    def container(self, name):
        try:
            p = subprocess.run(["distrobox", "list"], capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired):
            return False
        return any(name == (ln.split("|")[1].strip() if ln.count("|") >= 1 else "") for ln in p.stdout.splitlines()[1:])

    def which(self, tool):
        import shutil
        return shutil.which(tool)

    def linux_deps(self, d):
        return os.path.isfile(os.path.join(d, "deps32", "lib", "libSDL2.a"))

    def linux_build_dir(self, d):
        return os.path.isfile(os.path.join(d, "build", "CMakeCache.txt"))
