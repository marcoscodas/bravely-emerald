"""Builder: what can be built on this host, the prerequisite checks, the commands for the existing scripts
(tools/deck/build_deck.sh, tools/fetch_assets.sh, tools/windows/*) and the runner that executes them. No UI code."""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

from .constants import (DOWNLOAD_MB, SDL2_VERSION, SDL2_IMAGE_VERSION, MSYS_PACKAGES, MSYS_INSTALLER_URL, MSYS_INSTALLER_FILE, MIN_FREE_GB, HARD_MIN_FREE_GB,
                        CONTAINER_RE)
from .errors import container_hint, explain
from .host import detect_host
from . import report
from .probe import Probe, find_msys_root, linux_tool_version, podman_logs
from .repo import is_repo
from .settings import load_settings
from .tasks import Step, Task, kill_tree
from .validate import source_path_problem, clean_path, validate_rom, free_gb, network_ok, fmt_secs, sha256_file

CONTAINER_START_CHECK = "    checking that the build container starts"
CONTAINER_STARTED = "    build container started"
CONTAINER_START_FAILED = "ERROR: the container does not start."
CONTAINER_TIMESTAMP = re.compile(r" \[([0-9]{1,10}[.,][0-9]{6})\]$")


def _container_time(line):
    timestamp = CONTAINER_TIMESTAMP.search(line)
    return float(timestamp[1].replace(",", ".")) if timestamp else None


class Builder:
    """One instance per window/CLI run. Runs one task at a time."""

    def __init__(self, repo, dry_run=False, prefix=None):
        self.repo = repo
        self.host = detect_host()
        self.dry_run = dry_run
        self.prefix = prefix or []
        self.task = None
        self.lock = threading.Lock()
        self.net_cache = (0, False)
        self.last_checks = None        # result of the last checks(), reused when planning a build
        self.pkg_missing = None        # missing MSYS2 packages from the last check (None: unknown)
        self.msys_root = None
        self.probe = Probe(self)       # tests replace it with a fake host
        self.retry_wait = 10           # seconds per retry attempt after a mirror or network problem (tests set 0)
        self.manifest = None           # manifest of the bundled source (set by the caller), shown in the log header

    # ----- environment helpers
    def settings(self):
        return load_settings()

    def msys(self):
        return self.msys_root_found()[0]

    def msys_root_found(self):
        """(root, how): how names the way an existing MSYS2 was found, None when it is not installed (root = where it will go)."""
        if self.msys_root:
            return self.msys_root, ("set by the caller" if os.path.isfile(os.path.join(self.msys_root, "usr", "bin", "bash.exe")) else None)
        return find_msys_root(load_settings().get("msys_root"))

    def msys_bash(self):
        return os.path.join(self.msys(), "usr", "bin", "bash.exe")

    def msys_cmd(self, script, args=(), msystem="MSYS"):
        """argv + env that run `script` in an MSYS2 login shell; paths go in as $1.. (no string splicing)."""
        argv = [self.msys_bash(), "-lc", script, "builder"] + [str(a) for a in args]
        return argv, {"MSYSTEM": msystem, "CHERE_INVOKING": "1"}

    def default_dir(self):
        if self.host["kind"] == "windows":
            home = os.path.expanduser("~")
            docs = os.path.join(home, "Documents")
            return os.path.join(docs if os.path.isdir(docs) else home, "Bravely Emerald")
        return os.path.join(os.path.expanduser("~"), "emerald-deck")

    def have_assets(self):
        r = self.repo
        return (os.path.isfile(os.path.join(r, ".assets_ok")) and
                os.path.isfile(os.path.join(r, "graphics", "pokemon", "bulbasaur", "front.png")) and
                os.path.isfile(os.path.join(r, "assets", "ui2", "ui_atlas.bin")))

    def have_net(self):
        now = time.time()
        if now - self.net_cache[0] > 20:
            self.net_cache = (now, network_ok())
        return self.net_cache[1]

    # ----- targets
    def targets(self):
        k = self.host["kind"]
        win_ok = k == "windows"
        lin_ok = k in ("steamos", "linux") and self.host["arch"] in ("x86_64", "amd64")
        return [
            {"id": "windows", "label": "Windows exe (32-bit)", "available": win_ok,
             "why": "Native build on this PC with MSYS2 (mingw32)." if win_ok else
             "Not available here: the Windows build runs natively on Windows (MSYS2). Cross building from Linux has not been verified."},
            {"id": "linux32", "label": "Linux / Steam Deck (32-bit x86)", "available": lin_ok,
             "why": "Native build in a distrobox container (tools/deck/build_deck.sh)." if lin_ok else
             "Not available here: the Linux build needs a Linux x86_64 host with distrobox. Building it from Windows (WSL) has not been verified."},
        ]

    # ----- plan: what is present, missing or outdated, and what a build would do about it
    def plan(self, target=None, rom_needed=None):
        """One row per component: id, label, state (present|missing|outdated|unknown), mb (download), detail, todo."""
        k = self.host["kind"]
        target = target or ("windows" if k == "windows" else "linux32")
        pr = self.probe
        rows = []

        def row(id_, label, state, detail, todo, mb=0):
            rows.append({"id": id_, "label": label, "state": state, "detail": detail,
                         "todo": todo if state != "present" else "nothing: already installed, skipped", "mb": mb if state != "present" else 0})

        if target == "windows":
            root, how = pr.msys()
            if how:
                row("msys2", "MSYS2", "present", "%s (found: %s)" % (root, how), "")
            else:
                row("msys2", "MSYS2", "missing", "not found; it will go to %s" % root,
                    "download the official installer and install it silently", DOWNLOAD_MB["msys2"])
            if not how:
                row("packages", "MSYS2 packages", "missing", "all %d" % len(MSYS_PACKAGES), "update MSYS2, then install the packages", DOWNLOAD_MB["packages"])
            else:
                missing, err = pr.packages_missing()
                if err:
                    row("packages", "MSYS2 packages", "unknown", "pacman could not be queried: %s" % err, "run the install (pacman skips what is installed)", DOWNLOAD_MB["packages"])
                elif missing:
                    row("packages", "MSYS2 packages", "missing", "missing: %s" % ", ".join(missing), "update MSYS2, install only the missing packages", DOWNLOAD_MB["packages"] * len(missing) // len(MSYS_PACKAGES))
                else:
                    row("packages", "MSYS2 packages", "present", "all %d installed" % len(MSYS_PACKAGES), "")
            d = pr.deps()
            row("deps", "SDL2 %s + SDL2_image %s (static)" % (SDL2_VERSION, SDL2_IMAGE_VERSION), d,
                {"present": "build-win/deps, versions match", "missing": "not built", "outdated": "half built or another version"}[d],
                "build them (a few minutes)" if d == "missing" else "remove the old build-win/deps and build them again", DOWNLOAD_MB["deps"])
            row("tools", "Host tools (preproc, gbagfx)", "present" if pr.tools() else "missing", "tools/*/*.exe", "compile them (about a minute)")
            have_assets = pr.assets()
            row("assets", "Game files (from your ROM)", "present" if have_assets else "missing", ".assets_ok",
                "check the ROM, download and convert the files", DOWNLOAD_MB["assets"])
            b = pr.build_dir()
            row("build_dir", "CMake build folder (build-win/out)", b,
                {"present": "configured for this folder", "missing": "none yet: full compile", "outdated": "made for another folder: it is removed"}[b],
                "configure and compile everything" if b != "present" else "")
        else:
            for tool in ("podman", "distrobox"):
                have = pr.which(tool)
                row(tool, tool, "present" if have else "missing", have or "not found: install it with your package manager",
                    "install %s (needs your administrator password; the app never asks for it)" % tool)
            name = self.settings().get("container") or "emerald-build"
            if pr.which("distrobox"):
                ok = pr.container(name)
                row("container", "Container '%s'" % name, "present" if ok else "missing", "distrobox list",
                    "create it (Arch Linux image) and install the 32-bit toolchain", 400)
            else:
                row("container", "Container '%s'" % name, "missing", "distrobox is missing", "create it after distrobox is installed", 400)
            d = self.default_dir()
            row("deps32", "SDL2 + SDL2_image 32-bit (static)", "present" if pr.linux_deps(d) else "missing", "deps32/lib",
                "build them inside the container (a few minutes)", DOWNLOAD_MB["deps"])
            row("assets", "Game files (from your ROM)", "present" if pr.assets() else "missing", ".assets_ok",
                "check the ROM, download and convert the files", DOWNLOAD_MB["assets"])
            row("build_dir", "Build folder", "present" if pr.linux_build_dir(d) else "missing", "build/CMakeCache.txt", "configure and compile everything")
        return rows

    def plan_text(self, target=None):
        rows = self.plan(target)
        out = ["%-10s %-9s %8s  %s" % ("component", "state", "download", "what a build does")]
        for r in rows:
            out.append("%-10s %-9s %8s  %s  [%s]" % (r["id"], r["state"].upper(), ("%d MB" % r["mb"]) if r["mb"] else "-", r["todo"], r["detail"]))
        total = sum(r["mb"] for r in rows)
        out.append("Total download: about %d MB%s" % (total, "" if total else " (nothing to install)"))
        return chr(10).join(out)

    # ----- prerequisite checks
    def checks(self):
        k = self.host["kind"]
        items = []

        def add(id_, label, status, detail, action=None, manual=None, blocking=False, auto=False):
            items.append({"id": id_, "label": label, "status": status, "detail": detail,
                          "action": action, "manual": manual, "blocking": blocking and status == "bad", "auto": auto})

        gb = free_gb(self.repo)
        if gb >= MIN_FREE_GB:
            add("disk", "Free disk space", "ok", "%.0f GB free (at least %d GB recommended)." % (gb, MIN_FREE_GB))
        elif gb >= HARD_MIN_FREE_GB:
            add("disk", "Free disk space", "warn", "%.1f GB free; %d GB or more is recommended (build trees, SDL2, container)." % (gb, MIN_FREE_GB))
        else:
            add("disk", "Free disk space", "bad", "%.1f GB free; at least %d GB are needed." % (gb, HARD_MIN_FREE_GB), blocking=True)
        net = self.have_net()
        add("net", "Network (github.com)", "ok" if net else "bad",
            "Reachable." if net else "github.com is not reachable: packages, SDL2 and the game files are downloaded.", blocking=True)
        if k == "windows":
            self._checks_windows(add)
        elif k in ("steamos", "linux"):
            self._checks_linux(add)
        else:
            add("host", "Host", "bad", "Unsupported system: %s." % self.host["label"], blocking=True)
        self.last_checks = items
        return items

    def _checks_windows(self, add):
        gitwin = shutil.which("git")
        add("git", "Git", "ok" if gitwin else "info",
            ("Found: %s" % gitwin) if gitwin else "Git for Windows not on PATH. The build uses the MSYS2 'git' package instead "
            "(installed with the MSYS2 packages below). To clone this repository you need Git from https://git-scm.com.")
        bash = self.msys_bash()
        have_msys = os.path.isfile(bash)
        add("msys2", "MSYS2 (%s)" % self.msys(), "ok" if have_msys else "warn",
            "Found." if have_msys else "Not installed. The build installs it from the official installer (about 85 MB).",
            action=None if have_msys else "msys2", auto=True)
        if not have_msys:
            self.pkg_missing = None
            add("packages", "MSYS2 packages (mingw32 toolchain, cmake, make, python-pillow, ...)", "warn",
                "Not installed. The build installs them after MSYS2.", auto=True)
        else:
            missing, err = self._missing_packages()
            self.pkg_missing = None if err else missing
            if err:
                add("packages", "MSYS2 packages", "warn", "Could not query pacman (%s); the build will run the install anyway." % err, action="packages", auto=True)
            elif missing:
                add("packages", "MSYS2 packages", "warn", "Missing: %s. The build installs them." % ", ".join(missing), action="packages", auto=True)
            else:
                add("packages", "MSYS2 packages", "ok", "All %d required packages are installed." % len(MSYS_PACKAGES))
        deps = os.path.join(self.repo, "build-win", "deps", "lib")
        have_deps = os.path.isfile(os.path.join(deps, "libSDL2.a")) and os.path.isfile(os.path.join(deps, "libSDL2_image.a"))
        add("deps", "SDL2 2.32.10 + SDL2_image 2.8.12 (static, 32-bit)", "ok" if have_deps else "warn",
            "Built in build-win/deps." if have_deps else "Not built yet. The build makes them (a few minutes), or press Install.",
            action=None if have_deps else "deps", auto=True)
        add("assets", "Game files (graphics, sound, data from your ROM)", "ok" if self.have_assets() else "warn",
            "Present (.assets_ok): the ROM is not needed again." if self.have_assets() else
            "Not made yet. Pick your ROM below; the build downloads and checks the files.", auto=True)

    def _missing_packages(self):
        # a name is satisfied when it is an installed package, or a package group whose members are all installed
        script = ('for p in "$@"; do pacman -Q "$p" >/dev/null 2>&1 && continue; '
                  'n=$(pacman -Sg "$p" 2>/dev/null | wc -l); '
                  '[ "$n" -gt 0 ] && [ "$(pacman -Qg "$p" 2>/dev/null | wc -l)" -eq "$n" ] && continue; '
                  'echo "MISSING $p"; done')
        argv, env = self.msys_cmd(script, MSYS_PACKAGES)
        try:
            p = subprocess.run(argv, env=dict(os.environ, **env), capture_output=True, text=True, timeout=180,
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        except (OSError, subprocess.TimeoutExpired) as e:
            return [], str(e)
        if p.returncode != 0 and "MISSING" not in p.stdout:
            return [], (p.stdout + p.stderr).strip()[:200] or "pacman failed"
        return re.findall(r"^MISSING (\S+)", p.stdout, re.M), None

    def _checks_linux(self, add):
        arch_ok = self.host["arch"] in ("x86_64", "amd64")
        add("arch", "CPU architecture", "ok" if arch_ok else "bad",
            self.host["arch"] if arch_ok else "%s: the Linux build is for x86_64 only." % self.host["arch"], blocking=True)
        idl = self.host.get("id_like", "")
        if "arch" in idl or self.host["kind"] == "steamos":
            cmd = "sudo pacman -S distrobox podman"
        elif "debian" in idl or "ubuntu" in idl:
            cmd = "sudo apt install distrobox podman"
        elif "fedora" in idl or "rhel" in idl:
            cmd = "sudo dnf install distrobox podman"
        elif "suse" in idl:
            cmd = "sudo zypper install distrobox podman"
        else:
            cmd = "Install distrobox and podman with your package manager (https://distrobox.it)"
        for tool in ("distrobox", "podman"):
            have = shutil.which(tool)
            if self.host["kind"] == "steamos" and not have:
                manual = "SteamOS ships %s. Run a SteamOS update in Settings > System." % tool
            else:
                manual = cmd + "   (needs your administrator password: run it in a terminal)"
            add(tool, tool, "ok" if have else "bad", ("Found: %s" % have) if have else "Not found.",
                manual=None if have else manual, blocking=True)
        if shutil.which("distrobox"):
            name = self.settings().get("container") or "emerald-build"
            try:
                p = subprocess.run(["distrobox", "list"], capture_output=True, text=True, timeout=30)
                exists = any(name == (ln.split("|")[1].strip() if "|" in ln and len(ln.split("|")) > 1 else "") for ln in p.stdout.splitlines()[1:])
            except (OSError, subprocess.TimeoutExpired):
                exists = False
            add("container", "Build container '%s'" % name, "ok" if exists else "warn",
                "Exists." if exists else "Will be created by the build (Arch Linux image, about 400 MB, then the 32-bit toolchain).", auto=True)
        add("deps32", "SDL2 + SDL2_image (32-bit, static) and the toolchain", "info",
            "Made by the build inside the container (steps 2 and 3 of build_deck.sh). Finished steps are skipped on a rerun.", auto=True)
        add("assets", "Game files (graphics, sound, data from your ROM)", "ok" if self.have_assets() else "warn",
            "Present (.assets_ok): the ROM is not needed again." if self.have_assets() else
            "Not made yet. Pick your ROM below; the build downloads and checks the files.", auto=True)
        add("sudo", "Administrator password", "info",
            "Not needed. The commands that need root run inside the container. If anything ever asks for a password, "
            "run that command in a terminal yourself; this app never asks for or stores passwords.", auto=True)

    # ----- installs (Windows only)
    def msys2_prepare(self):
        size = None
        try:
            req = urllib.request.Request(MSYS_INSTALLER_URL, method="HEAD")
            with urllib.request.urlopen(req, timeout=15) as r:
                size = int(r.headers.get("Content-Length") or 0) or None
        except Exception as e:                       # offline: still show the consent with the unknown size
            size = None
        return {"filename": MSYS_INSTALLER_FILE, "url": MSYS_INSTALLER_URL, "size": size,
                "dest": os.path.join(tempfile.gettempdir(), "bravely-builder-dl"),
                "root": self.msys(),
                "text": "Download the official MSYS2 installer and run it silently into %s." % self.msys()}

    def plan_install(self, what, consent=False):
        """Return (label, stages, steps, meta) for an install action."""
        label, stages, steps = self._plan_install(what, consent)
        logdir = os.path.join(tempfile.gettempdir() if what == "msys2" else os.path.join(self.repo, "build-win"), "bravely-builder-logs" if what == "msys2" else "builder-logs")
        return label, stages, steps, {"logdir": logdir, "low": False}

    def _plan_install(self, what, consent=False):
        if self.host["kind"] != "windows":
            raise ValueError("Install actions exist for Windows only; on Linux run the shown package command in a terminal.")
        if what == "msys2":
            if not consent:
                raise ValueError("The download needs your consent.")
            return "Install MSYS2", ["download", "install"], [("msys2", None)]
        if what == "packages":
            if not os.path.isfile(self.msys_bash()):
                raise ValueError("Install MSYS2 first.")
            s1, e1 = self.msys_cmd('pacman --noconfirm -Syu')
            s2, e2 = self.msys_cmd('pacman --noconfirm -S --needed "$@"', MSYS_PACKAGES)
            return "Install MSYS2 packages", ["packages"], [
                Step("pacman -Syu (system update; may report that a restart of MSYS2 is needed, that is fine)", "packages", s1, e1,
                     tolerate=lambda lines: True),
                Step("pacman -S the build packages", "packages", s2, e2)]
        if what == "deps":
            if not os.path.isfile(self.msys_bash()):
                raise ValueError("Install MSYS2 first.")
            argv, env = self.msys_cmd('cd "$(cygpath -u "$1")" && bash tools/windows/build_deps.sh', [self.repo], "MINGW32")
            return "Build SDL2 and SDL2_image", ["deps"], [Step("SDL2 2.32.10 + SDL2_image 2.8.12 (static, 32-bit)", "deps", argv, env)]
        raise ValueError("unknown install item")

    # ----- build planning
    def plan_build(self, opts):
        """Validate the options and return (label, stages, [Step], meta). Raises ValueError with a readable message."""
        kinds = [t for t in opts.get("targets", []) if t in ("windows", "linux32")]
        if len(kinds) != 1:
            raise ValueError("Pick exactly one target (one target is built per run).")
        target = kinds[0]
        avail = {t["id"]: t for t in self.targets()}
        if not avail[target]["available"]:
            raise ValueError(avail[target]["why"])
        jobs = opts.get("jobs")
        if not isinstance(jobs, int) or isinstance(jobs, bool) or not 1 <= jobs <= 64:
            raise ValueError("Jobs must be a number from 1 to 64.")
        low = bool(opts.get("low_priority"))
        rom = ""
        if not self.probe.assets() or opts.get("force_assets"):
            rom = clean_path(opts.get("rom", ""), "ROM path", no_space=(target == "linux32"))
            v = validate_rom(rom)
            if not v["ok"]:
                raise ValueError("ROM: " + v["message"])
        problem = source_path_problem(self.repo, self.host["kind"] == "windows")
        if problem:
            raise ValueError(problem)
        if target == "windows":
            return self._plan_windows(rom, jobs, low, opts)
        return self._plan_linux(rom, jobs, low, opts)

    def _plan_windows(self, rom, jobs, low, opts):
        if not self.dry_run:
            blockers = [c for c in (self.last_checks or self.checks()) if c["blocking"]]
            if blockers:
                raise ValueError("Cannot build yet: " + "; ".join("%s (%s)" % (c["label"], c["detail"]) for c in blockers))
        steps = []
        rows = {r["id"]: r for r in self.plan("windows")}
        have_msys = rows["msys2"]["state"] == "present"
        need_pkgs = rows["packages"]["state"] != "present"

        def noted(text):
            return Step(text, "prereq", [], func=lambda task, t=text: task.log("already installed / up to date: skipped. " + t))

        if have_msys:
            steps.append(noted("MSYS2 is installed: %s" % rows["msys2"]["detail"]))
        else:
            steps.append(Step("Install MSYS2 (official installer)", "install", [], func=self._msys2_step))
        if need_pkgs:
            sync = lambda task: self._pacman_sync(task)
            steps.append(Step("Update MSYS2 (repeated until pacman has nothing left to do)", "install", [], func=sync))
            s2, e2 = self.msys_cmd('pacman --noconfirm --needed -S "$@"', MSYS_PACKAGES)
            steps.append(Step("Install the missing build packages (retried up to 3 times if a mirror times out)", "install", s2, e2, retries=3))
        else:
            steps.append(noted("all %d MSYS2 packages are installed: no pacman run" % len(MSYS_PACKAGES)))
        bash_rules = [
            (r"=== Building the host tools", "tools"), (r"=== Converting the game files", "assets"),
            (r"=== Intro margins", "assets"), (r"=== SDL2 and SDL2_image", "deps"),
            (r"=== Configuring CMake", "compile"), (r"=== Building pokeemerald\.exe", "compile"),
            (r"Linking (C|CXX) executable|\bld\b.*pokeemerald\.exe", "link"),
        ]
        if rom:
            fetch = 'cd "$(cygpath -u "$1")" && FETCH_NO_CONVERT=1 bash ./tools/fetch_assets.sh "$(cygpath -u "$2")"'
            a, e = self.msys_cmd(fetch, [self.repo, rom], "MINGW64")
            steps.append(Step("Game files, pass 1 (checks the ROM, downloads the files; stops at the port's files, as documented)", "files", a, e,
                              tolerate=lambda lines: any("Generating the port's files" in l for l in lines) and any("=== Copying" in l for l in lines)))
            if rows["tools"]["state"] == "present":
                steps.append(noted("host tools (preproc, gbagfx) are built"))
            else:
                a, e = self.msys_cmd('cd "$(cygpath -u "$1")" && make -f make_tools.mk -j"$2" CC="gcc -static" CXX="g++ -static"', [self.repo, jobs], "MINGW64")
                steps.append(Step("Host tools (static)", "tools", a, e))
            a, e = self.msys_cmd('cd "$(cygpath -u "$1")" && make -f vita/assets.mk -j"$2"', [self.repo, jobs], "MINGW64")
            steps.append(Step("Converting the game's graphics and data", "assets", a, e))
            a, e = self.msys_cmd('cd "$(cygpath -u "$1")" && bash ./tools/fetch_assets.sh "$(cygpath -u "$2")"', [self.repo, rom], "MINGW64")
            steps.append(Step("Game files, pass 2 (UI atlas)", "files", a, e))
        # build.sh takes its job count from nproc; GNU nproc honours OMP_NUM_THREADS
        a, e = self.msys_cmd('export OMP_NUM_THREADS="$2"; cd "$(cygpath -u "$1")" && tools/windows/build.sh', [self.repo, jobs], "MSYS")
        steps.append(Step("tools/windows/build.sh", "tools", a, e, stage_rules=bash_rules))
        a, e = self.msys_cmd('cd "$(cygpath -u "$1")" && tools/windows/package.sh', [self.repo], "MSYS")
        steps.append(Step("tools/windows/package.sh", "package", a, e))
        out = clean_path(opts.get("dir") or self.default_dir(), "Install folder", no_space=False)
        repo_n = os.path.normcase(os.path.abspath(self.repo))
        if os.path.normcase(os.path.abspath(out)) == repo_n or os.path.normcase(os.path.abspath(out)).startswith(repo_n + os.sep):
            raise ValueError("The install folder must be outside the source folder.")
        steps.append(Step("Copy the game to the install folder (saves and settings are kept)", "package", [], func=lambda task: install_windows_result(task, self.repo, out)))
        stages = ["prereq"] + (["install"] if (not have_msys or need_pkgs) else []) + ["files", "tools", "assets", "deps", "compile", "link", "package"]
        meta = {"target": "windows", "dir": out, "logdir": os.path.join(self.repo, "build-win", "builder-logs"),
                "low": low}
        return "Windows build", stages, steps, meta

    def _plan_linux(self, rom, jobs, low, opts):
        d = clean_path(opts.get("dir") or self.default_dir(), "Install folder")
        container = opts.get("container") or "emerald-build"
        if not isinstance(container, str) or not CONTAINER_RE.match(container):
            raise ValueError("Container name: letters, digits, '.', '_' and '-' only.")
        if not self.dry_run:
            blockers = [c for c in self.checks() if c["blocking"]]
            if blockers:
                raise ValueError("Prerequisites missing: " + "; ".join("%s (%s)" % (c["label"], c["detail"]) for c in blockers))
        argv = ["bash", os.path.join(self.repo, "tools", "deck", "build_deck.sh"), "--dir", d,
                "--jobs", str(jobs), "--container", container]
        if rom:
            argv += ["--rom", rom]
        if not opts.get("run_test"):
            argv.append("--no-run")
        rules = [
            (r"^=== 1/6", "prereq"), (r"^=== 2/6", "container"), (r"^=== 3/6", "deps"), (r"^=== 4/6", "files"),
            (r"^=== 5/6", "compile"), (r"Linking (C|CXX) executable", "link"), (r"^=== 6/6", "package"),
            (r"^=== Start-up test", "test"),
        ]
        stages = ["prereq", "container", "deps", "files", "compile", "link", "package"] + (["test"] if opts.get("run_test") else [])
        steps = [Step("tools/deck/build_deck.sh", "prereq", argv, {}, stage_rules=rules)]
        meta = {"target": "linux32", "dir": d, "logdir": os.path.join(d, "builder-logs"), "low": low,
                "container": container}
        return "Linux / Steam Deck build", stages, steps, meta

    # ----- repository
    def set_repo(self, path):
        if not is_repo(path):
            raise ValueError("%s does not contain the build scripts (tools/fetch_assets.sh, tools/deck, tools/windows)." % path)
        self.repo = os.path.abspath(path)

    # ----- running
    def start(self, label, stages, steps, meta, kind="build", listener=None):
        with self.lock:
            if self.task and self.task.state == "running":
                raise ValueError("Another task is running. Wait for it or press Cancel.")
            ts = time.strftime("%Y%m%d-%H%M%S")
            logfile = os.path.join(meta["logdir"], "%s-%s.log" % (kind, ts)) if meta and meta.get("logdir") and not self.dry_run else None
            task = Task(kind, label, stages, logfile, self.dry_run)
            task.meta = meta or {}
            self.task = task
        if listener:
            task.listeners.append(listener)
        threading.Thread(target=self._run, args=(task, steps), daemon=True).start()
        return task

    def _spawn(self, step, low):
        argv = list(step.argv)
        env = dict(os.environ)
        env.update(step.env)
        env["PYTHONUNBUFFERED"] = "1"
        kw = {}
        if os.name == "nt":
            flags = getattr(subprocess, "CREATE_NO_WINDOW", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
            if low:
                flags |= getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)
            kw["creationflags"] = flags
        else:
            kw["start_new_session"] = True
            if low and shutil.which("nice"):
                argv = ["nice", "-n", "19"] + argv
            argv = self.prefix + argv
        return subprocess.Popen(argv, cwd=step.cwd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, **kw), argv

    def _run(self, task, steps):
        low = task.meta.get("low", False)
        tail = []
        try:
            if task.kind == "build":
                for ln in report.header(self, self.manifest):
                    task.log(ln)
                if self._is_desktop_linux_build(task) and not self.dry_run:
                    for tool, label in (("distrobox", "Distrobox"), ("podman", "Podman")):
                        version = linux_tool_version(tool)
                        if version:
                            task.log("%s: %s" % (label, version))
                task.log("")
            task.log("# %s, started %s%s" % (task.label, time.strftime("%Y-%m-%d %H:%M:%S"), "  [DRY RUN: nothing is executed]" if self.dry_run else ""))
            if steps and isinstance(steps[0], tuple):          # the MSYS2 installer download
                ok = self._run_msys2_install(task)
                task.finish("done" if ok else "failed", hints=[] if ok else explain(tail))
                return
            task.set_stage("prereq")
            for step in steps:
                if task.cancel_requested:
                    break
                task.set_stage(step.stage)
                task.cur_step = step.name
                task.log("")
                task.log("##### %s" % step.name)
                if step.func:
                    task.log("(in-process step)")
                else:
                    task.log("$ " + subprocess.list2cmdline(step.argv))
                if self.dry_run:
                    time.sleep(0.05)
                    task.log("[dry run] not executed")
                    continue
                if step.func:
                    try:
                        step.func(task)
                    except Exception as e:
                        task.log("ERROR: %s" % e)
                        task.finish("failed", hints=[{"title": "The step failed.", "fix": str(e)}], step=step.name)
                        return
                    continue
                rc, lines = self._run_proc(task, step, low, tail)
                if rc is None:                      # could not start
                    return
                if task.cancel_requested:
                    break
                if rc != 0 and not (step.tolerate and step.tolerate(lines)):
                    hints = self._failure_hints(task, lines, tail)
                    if task.cancel_requested:
                        break
                    task.hints = hints
                    task.finish("failed", hints=hints, code=rc, step=step.name)
                    return
                if rc != 0:
                    task.log("(exit code %s accepted for this step)" % rc)
            if task.cancel_requested:
                task.log("##### CANCELLED")
                task.finish("cancelled")
                return
            task.result = self.collect_result(task)
            if task.kind == "build" and not self.dry_run and not task.result.get("ok"):
                task.log("##### The scripts finished but the result is incomplete: %s" % task.result.get("problem"))
                task.finish("failed", hints=[{"title": "The build ended without the expected files.", "fix": task.result.get("problem", "")}])
                return
            task.log("")
            task.log("##### DONE in %s" % fmt_secs(time.time() - task.started))
            task.finish("done", result=task.result)
        except Exception as e:           # never leave the UI waiting
            task.log("INTERNAL ERROR: %r" % (e,))
            task.finish("failed", hints=[{"title": "Internal error in the builder.", "fix": repr(e)}])

    def _is_desktop_linux_build(self, task):
        return self.host["kind"] == "linux" and task.meta.get("target") == "linux32"

    def _failure_hints(self, task, lines, tail):
        if task.meta.get("target") != "linux32" or task.stage != "container":
            return explain(tail)
        recent = lines[-300:]
        enter_failure = next((line for line in reversed(recent) if line.startswith(CONTAINER_START_FAILED)), "")
        start_marker = CONTAINER_START_CHECK if enter_failure else CONTAINER_STARTED
        start_index = next((i for i in range(len(lines) - 1, -1, -1) if lines[i].startswith(start_marker)), None)
        container = task.meta.get("container") or "emerald-build"
        if start_index is not None:
            recent = lines[max(start_index + 1, len(lines) - 300):]
        if enter_failure and self._is_desktop_linux_build(task):
            if start_index is None:
                # An older script can mix errors from create and enter in the same stage.
                recent = []
            else:
                started = _container_time(lines[start_index])
                finished = _container_time(enter_failure)
                if started is not None and finished is not None:
                    recent += podman_logs(container, started, finished)

        hint = container_hint(recent)
        if hint:
            return [hint]
        if enter_failure or any(line.startswith("ERROR: distrobox create failed") for line in recent):
            name = container if CONTAINER_RE.fullmatch(container) else "CONTAINER_NAME"
            return [{"title": "The build container failed for an unknown reason.",
                     "fix": "If Distrobox uses Podman, run 'podman logs --tail 100 %s' in a terminal to inspect the log locally. "
                            "Review the output for private information before sharing any lines." % name}]
        return explain(tail)

    RETRY_RE = re.compile(r"failed retrieving file|Operation too slow|Connection timed out|Could not resolve host|"
                          r"error: failed to (retrieve|commit)|download library error|SSL connect error|Connection reset|"
                          r"unable to lock database|Maximum file size exceeded|curl: \(\d+\)", re.I)

    def _run_proc(self, task, step, low, tail):
        """Run one subprocess step, retrying up to step.retries times when the output shows a network or mirror problem.
        Returns (exit code, lines); (None, []) when the program could not be started (the task is finished as failed)."""
        desktop_linux = self._is_desktop_linux_build(task)
        for attempt in range(step.retries + 1):
            try:
                proc, real = self._spawn(step, low)
            except OSError as e:
                task.log("ERROR: cannot start: %s" % e)
                task.finish("failed", hints=[{"title": "The program could not be started.", "fix": str(e)}], step=step.name)
                return None, []
            task.proc = proc
            lines = []
            hide_container_output = False
            skip_entrypoint_dump = False
            for raw in iter(proc.stdout.readline, b""):
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                if desktop_linux and task.stage == "container":
                    if line == CONTAINER_STARTED or line.startswith(CONTAINER_START_FAILED):
                        hide_container_output = skip_entrypoint_dump = False
                    elif skip_entrypoint_dump:
                        continue
                    elif hide_container_output and "Error: could not start entrypoint." in line:
                        # Distrobox dumps all historical Podman logs here, without a time or line limit.
                        task.log("Distrobox could not start the container entrypoint.")
                        skip_entrypoint_dump = True
                        continue
                    elif line.startswith(CONTAINER_START_CHECK):
                        task.log(line)
                        hide_container_output = True
                        lines.append(line)
                        continue
                lines.append(line)
                if hide_container_output:
                    continue
                tail.append(line)
                if len(tail) > 600:
                    del tail[:200]
                task.log(line)
                for pat, st in step.stage_rules:
                    if re.search(pat, line):
                        task.set_stage(st)
                m = re.match(r"^\[\s*(\d+)%\]", line)
                if m:
                    task.emit("progress", pct=int(m.group(1)))
            proc.wait()
            proc.stdout.close()
            task.proc = None
            rc = proc.returncode
            if rc == 0 or task.cancel_requested or attempt >= step.retries or not self.RETRY_RE.search("\n".join(lines[-80:])):
                return rc, lines
            task.log("")
            task.log("The download did not work (the mirror or the network is slow). Trying again, attempt %d of %d, in %d seconds." %
                     (attempt + 2, step.retries + 1, self.retry_wait * (attempt + 1)))
            for _ in range(self.retry_wait * (attempt + 1)):
                if task.cancel_requested:
                    return rc, lines
                time.sleep(1)
        return rc, lines

    def _pacman_sync(self, task):
        """First sync on a fresh MSYS2, following https://www.msys2.org/docs/updating/ : the core update may end the shell
        (restart needed), so repeat pacman -Suy until it has nothing left to do (at most 4 rounds)."""
        root = self.msys()
        low = task.meta.get("low", False)
        self._wait_pacman_lock(task, root)
        init = ('[ -f /etc/pacman.d/gnupg/trustdb.gpg ] || { echo "Setting up the pacman keyring"; pacman-key --init && pacman-key --populate msys2; }; '
                'echo "MSYS2 shell ready (home: $HOME)"')
        a, e = self.msys_cmd(init)
        rc, _ = self._run_proc(task, Step("keyring", "install", a, e), low, [])
        if rc not in (0, None):
            raise RuntimeError("MSYS2 could not set up its package keyring (exit code %s). Close other MSYS2 windows, check that the "
                               "clock of this PC is right, and try again." % rc)
        for rnd in range(1, 5):
            if task.cancel_requested:
                return
            task.log("")
            task.log("-- pacman -Suy, round %d (a restart of MSYS2 in the middle is normal)" % rnd)
            a, e = self.msys_cmd("pacman --noconfirm -Suy")
            rc, lines = self._run_proc(task, Step("pacman -Suy", "install", a, e, retries=3), low, [])
            text = "\n".join(lines)
            if rc == 0 and ("there is nothing to do" in text and "starting full system upgrade" in text.lower()) :
                task.log("-- system is up to date")
                return
            if rc == 0 and rnd > 1 and "there is nothing to do" in text:
                return
            if rc not in (0, None) and not re.search(r"terminate|restart|msys2-runtime|core system", text, re.I) and rnd >= 2:
                raise RuntimeError("pacman -Suy failed (exit code %s); see the lines above." % rc)
        task.log("-- the update ran 4 rounds; going on with the package install")

    def _wait_pacman_lock(self, task, root):
        lock = os.path.join(root, "var", "lib", "pacman", "db.lck")
        if not os.path.exists(lock):
            return
        task.log("pacman lock file found: %s" % lock)
        for i in range(30):
            if not self._pacman_running():
                try:
                    os.remove(lock)
                    task.log("No pacman is running: the old lock file was removed.")
                except OSError as e:
                    raise RuntimeError("Cannot remove %s: %s. Close MSYS2 windows and try again." % (lock, e))
                return
            if i == 0:
                task.log("Another pacman is running. Waiting for it (up to 5 minutes).")
            time.sleep(10)
        raise RuntimeError("Another pacman keeps running. Close the MSYS2 windows (or wait for the update) and try again.")

    @staticmethod
    def _pacman_running():
        if os.name != "nt":
            return False
        try:
            p = subprocess.run(["tasklist", "/FI", "IMAGENAME eq pacman.exe", "/NH"], capture_output=True, text=True, timeout=20,
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            return "pacman.exe" in p.stdout
        except (OSError, subprocess.TimeoutExpired):
            return False

    def _msys2_step(self, task):
        if not self._run_msys2_install(task):
            raise RuntimeError("The MSYS2 installer did not finish (see the details).")

    def _published_sha256(self, task):
        """The checksum MSYS2 publishes next to the installer, or None when it cannot be read."""
        try:
            req = urllib.request.Request(MSYS_INSTALLER_URL + ".sha256", headers={"User-Agent": "bravely-builder"})
            with urllib.request.urlopen(req, timeout=30) as r:
                m = re.match(r"\s*([0-9a-fA-F]{64})", r.read(400).decode("ascii", "replace"))
            if m:
                task.log("Published checksum (msys2-installer release): %s" % m.group(1).lower())
                return m.group(1).lower()
        except Exception as e:
            task.log("Could not read the published checksum (%s); the download is only checked for size." % e)
        return None

    def _download(self, task, dest, expected):
        part = dest + ".part"
        try:
            req = urllib.request.Request(MSYS_INSTALLER_URL, headers={"User-Agent": "bravely-builder"})
            with urllib.request.urlopen(req, timeout=60) as r, open(part, "wb") as out:
                total = int(r.headers.get("Content-Length") or 0)
                got, last = 0, 0
                while True:
                    if task.cancel_requested:
                        task.log("cancelled")
                        return False
                    chunk = r.read(1 << 18)
                    if not chunk:
                        break
                    out.write(chunk)
                    got += len(chunk)
                    if total and got * 100 // total >= last + 10:
                        last = got * 100 // total
                        task.log("  %d%% (%d of %d MB)" % (last, got >> 20, total >> 20))
            if total and os.path.getsize(part) != total:
                task.log("ERROR: the download is incomplete (%d of %d bytes). Check the connection and try again." % (os.path.getsize(part), total))
                return False
            got_sha = sha256_file(part)
            task.log("Downloaded %d bytes, sha256 %s" % (os.path.getsize(part), got_sha))
            if expected and got_sha != expected:
                task.log("ERROR: the installer does not match the published checksum. It was deleted; try again later.")
                os.remove(part)
                return False
            os.replace(part, dest)
            return True
        except Exception as e:
            task.log("ERROR: download failed: %s. Check the internet connection; a proxy or antivirus may be blocking github.com." % e)
            return False

    def _run_msys2_install(self, task):
        """Download the official installer (consent was given in the UI) and run it silently."""
        dest_dir = os.path.join(tempfile.gettempdir(), "bravely-builder-dl")
        os.makedirs(dest_dir, exist_ok=True)
        dest = os.path.join(dest_dir, MSYS_INSTALLER_FILE)
        task.set_stage("install")
        task.log("MSYS2 installer: %s" % MSYS_INSTALLER_URL)
        if self.dry_run:
            task.log("[dry run] would download to %s and run: %s in --confirm-command --accept-messages --root %s" % (dest, dest, self.msys()))
            return True
        local = os.environ.get("BRAVELY_MSYS2_INSTALLER")
        if local and os.path.isfile(local):
            dest = local
            task.log("Using the installer you named in BRAVELY_MSYS2_INSTALLER: %s" % local)
            task.log("sha256 %s (compare it with the checksum on https://github.com/msys2/msys2-installer/releases)" % sha256_file(dest))
        else:
            expected = self._published_sha256(task)
            if os.path.isfile(dest) and expected and sha256_file(dest) == expected:
                task.log("already downloaded and verified: %s (%d MB): not downloaded again" % (dest, os.path.getsize(dest) >> 20))
            elif not self._download(task, dest, expected):
                return False
        problem = source_path_problem(self.msys())
        if problem:
            task.log("ERROR: " + problem.replace("source folder", "MSYS2 folder"))
            return False
        task.set_stage("install")
        task.log("Running the installer silently (a Windows permission prompt may appear).")
        argv = [dest, "in", "--confirm-command", "--accept-messages", "--root", self.msys().replace("\\", "/")]
        task.log("$ " + subprocess.list2cmdline(argv))
        try:
            proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            task.proc = proc
            for raw in iter(proc.stdout.readline, b""):
                task.log(raw.decode("utf-8", errors="replace").rstrip())
            proc.wait()
            task.proc = None
        except OSError as e:
            task.log("ERROR: %s" % e)
            return False
        ok = proc.returncode == 0 and os.path.isfile(self.msys_bash())
        task.log("Installer exit code %s; bash.exe %s" % (proc.returncode, "found" if ok else "missing"))
        return ok

    def collect_result(self, task):
        meta = task.meta
        if meta.get("target") == "windows":
            d = meta.get("dir") or os.path.join(self.repo, "dist-win")
            exe = os.path.join(d, "pokeemerald.exe")
            zp = os.path.join(self.repo, "pokeemerald-win.zip")
            ok = os.path.isfile(exe) and os.path.isdir(os.path.join(d, "app0"))
            return {"ok": ok, "target": "windows", "folder": d, "exe": exe, "zip": zp if os.path.isfile(zp) else None,
                    "problem": None if ok else "dist-win/pokeemerald.exe or app0 is missing",
                    "launch": ok, "savedir": os.path.join(d, "userdata", "saves"), "log": task.logfile}
        if meta.get("target") == "linux32":
            d = meta["dir"]
            dist = os.path.join(d, "build", "dist")
            launcher = os.path.join(d, "emerald-launch.sh")
            ok = os.path.isfile(os.path.join(dist, "pokeemerald")) and os.path.isfile(launcher)
            return {"ok": ok, "target": "linux32", "folder": dist, "launcher": launcher, "zip": None,
                    "problem": None if ok else "build/dist/pokeemerald or emerald-launch.sh is missing",
                    "launch": ok, "savedir": os.path.join(d, "userdata", "saves"), "log": task.logfile,
                    "container": task.meta.get("container")}
        return {"ok": True}

    def cancel(self):
        t = self.task
        if not t or t.state != "running":
            return False
        t.cancel_requested = True
        t.log("##### Cancel requested: stopping the whole process tree")
        kill_tree(t.proc)
        return True

    def open_result(self, what):
        t = self.task
        res = t.result if t else None
        if not res or not res.get("ok"):
            raise ValueError("There is no finished build to open.")
        if what == "folder":
            path = res["folder"]
            if os.name == "nt":
                os.startfile(path)             # noqa: path comes from our own result, not from the request
            else:
                subprocess.Popen(["xdg-open", path], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
            return "Opened %s" % path
        if what == "launch":
            if os.name == "nt":
                subprocess.Popen([res["exe"]], cwd=res["folder"], close_fds=True,
                                 creationflags=getattr(subprocess, "DETACHED_PROCESS", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0))
            else:
                subprocess.Popen([res["launcher"]], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
            return "Started the game."
        raise ValueError("unknown action")


def install_windows_result(task, repo, out):
    """Copy repo/dist-win to the install folder. Existing saves and settings are never overwritten."""
    src = os.path.join(repo, "dist-win")
    if not os.path.isfile(os.path.join(src, "pokeemerald.exe")):
        raise RuntimeError("dist-win/pokeemerald.exe is missing; the package step did not finish")
    n = kept = 0
    for root, _, files in os.walk(src):
        rel_root = os.path.relpath(root, src)
        os.makedirs(os.path.normpath(os.path.join(out, rel_root)), exist_ok=True)
        for fn in files:
            rel = os.path.normpath(os.path.join(rel_root, fn)).replace("\\", "/")
            dst = os.path.join(out, rel)
            if os.path.exists(dst) and (rel.startswith("userdata/saves/") or rel == "userdata/settings.txt"):
                kept += 1
                continue
            shutil.copy2(os.path.join(root, fn), dst)
            n += 1
    os.makedirs(os.path.join(out, "userdata", "saves"), exist_ok=True)
    task.log("copied %d files to %s (%d existing save/settings files kept)" % (n, out, kept))
