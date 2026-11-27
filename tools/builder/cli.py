#!/usr/bin/env python3
"""Headless driver of the builder core (no window). Also reachable as flags of the app itself, so the packaged
exe can be tested without a display:  BravelyEmeraldBuilder.exe --selftest   |   --build --rom X --out DIR

    cli.py checks   [--repo DIR]
    cli.py plan     [--repo DIR] [--target windows|linux32] [--json]   (present / missing / outdated, download size, what a build does)
    cli.py rom FILE
    cli.py source   [--repo DIR] [--force]     (resolve the source; extract the bundle)
    cli.py selftest
    cli.py build --target windows|linux32 [--rom|--game FILE] [--jobs N] [--low] [--out DIR] [--repo DIR] [--dry-run]
    cli.py prepare --game FILE --out DIR [--allow-downloads] [--target ...] [--jobs N] [--low] [--repo DIR]
                   (RecompLauncher setup step: a build without a window that prints "progress 0.4 Text" lines; see docs/RECOMPLAUNCHER.md)

Without --repo the source is the bundled snapshot (extracted into the work folder), else a checkout (see core/source.py).
"""
import argparse
import json
import os
import shutil
import sys
import tempfile
import time
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from core import Builder, validate_rom          # noqa: E402
from core.validate import parts_note            # noqa: E402
from core.source import Source, SourceError     # noqa: E402


class SafeOut:
    """Wraps the output stream. A windowed exe can hold a handle that opens but cannot be written to (OSError 22);
    on the first failed write or flush the output moves to cli.log instead of crashing the build."""

    def __init__(self, stream):
        self.stream = stream
        self.fallback = None

    def _log(self):
        if self.fallback is None:
            from core.settings import app_home
            self.fallback = open(os.path.join(app_home(), "cli.log"), "w", encoding="utf-8", errors="replace")
        return self.fallback

    def write(self, text):
        if self.fallback is None:
            try:
                return self.stream.write(text)
            except (OSError, ValueError):
                pass
        return self._log().write(text)

    def flush(self):
        if self.fallback is None:
            try:
                return self.stream.flush()
            except (OSError, ValueError):
                pass
        self._log().flush()

    def isatty(self):
        return False


def setup_stdout():
    """A windowed (no console) exe has no stdout: use the redirected handle if there is one, else a log file.
    Either way writes are guarded by SafeOut."""
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stdout = sys.stderr = SafeOut(sys.stdout)
        return
    try:
        sys.stdout = sys.stderr = SafeOut(open(1, "w", encoding="utf-8", errors="replace", closefd=False))
    except OSError:
        from core.settings import app_home
        sys.stdout = sys.stderr = open(os.path.join(app_home(), "cli.log"), "w", encoding="utf-8")


def make_builder(repo_arg, dry_run=False, force=False):
    src = Source(force_repo=repo_arg)
    print("source:", src.describe(), flush=True)
    path = src.prepare(force=force, progress=lambda m: print("  " + m, flush=True))
    print("repository:", path, flush=True)
    b = Builder(path, dry_run)
    b.manifest = src.manifest
    return b, src


def write_synthetic_rom(path):
    buf = bytearray(16 * 1024 * 1024)
    buf[0xA0:0xAC] = b"POKEMON EMER"
    buf[0xAC:0xB0] = b"BPEE"
    buf[0xBD] = (0 - (sum(buf[0xA0:0xBD]) + 25)) & 255
    with open(path, "wb") as f:
        f.write(buf)


def run_task(b, label, stages, steps, meta):
    done = []

    def on_event(ev):
        if ev["type"] == "log":
            print(ev["text"], flush=True)
        elif ev["type"] == "stage":
            print("@@ stage:", ev["stage"], flush=True)
        elif ev["type"] == "state":
            done.append(ev)

    t = b.start(label, stages, steps, meta, "build", on_event)
    try:
        while not done:
            time.sleep(0.5)
    except KeyboardInterrupt:
        b.cancel()
        while not done:
            time.sleep(0.5)
    print("@@ final:", done[0]["state"], json.dumps(t.result) if t.result else "", flush=True)
    for h in t.hints:
        print("@@ hint:", h["title"], "-", h["fix"], flush=True)
    return 0 if done[0]["state"] == "done" else 1, t


def selftest():
    """Extract the bundled source, check it, and dry-run the build of this host's target (prints the commands)."""
    b, src = make_builder(None, dry_run=True)
    if src.mode != "bundled":
        print("SELFTEST FAILED: the source is not the bundled snapshot (mode %s)" % src.mode)
        return 1
    for rel in ("tools/fetch_assets.sh", "CMakeLists.txt", "assets/voxel/buildings.bin", "tools/windows/build.sh", "tools/deck/build_deck.sh",
                "design/stitch/_tools/export_assets.py", "vita/assets.mk"):
        ok = os.path.isfile(os.path.join(b.repo, *rel.split("/")))
        print("  %-40s %s" % (rel, "present" if ok else "MISSING"))
        if not ok:
            print("SELFTEST FAILED")
            return 1
    print("manifest:", json.dumps(src.manifest))
    target = "windows" if b.host["kind"] == "windows" else "linux32"
    rom = os.path.join(tempfile.mkdtemp(prefix="bb-selftest-"), "synthetic.gba")
    write_synthetic_rom(rom)
    opts = {"targets": [target], "rom": rom, "jobs": 2, "low_priority": True, "force_assets": True,
            "dir": os.path.join(os.path.dirname(rom), "out")}
    label, stages, steps, meta = b.plan_build(opts)
    print("stages:", " > ".join(stages))
    rc, t = run_task(b, label, stages, steps, meta)
    print("SELFTEST OK" if rc == 0 else "SELFTEST FAILED")
    return rc


PREPARE_MARKER = "prepared.txt"          # written into --out when a prepare run finished; recomplauncher.json lists it in prepare.done
STAGE_TEXT = {"prereq": "Checking the build tools", "install": "Installing the build tools", "container": "Preparing the build container",
              "files": "Fetching and converting the game files", "tools": "Building the host tools", "assets": "Converting graphics and data",
              "deps": "Building SDL2", "compile": "Compiling the game", "link": "Linking the game", "package": "Copying the game to its folder",
              "test": "Testing the start-up"}


def progress_line(fraction, text):
    """The line RecompLauncher reads from a setup step: progress <0..1> <text>."""
    return "progress %.2f %s" % (max(0.0, min(1.0, fraction)), text)


def stage_fraction(stages, stage):
    """Share of the build behind us when `stage` starts: its place in the plan's stage list, never 1 before the end."""
    if stage not in stages:
        return 0.0
    return min(0.99, stages.index(stage) / float(len(stages)))


def rom_from_zip(path, dest_dir):
    """A zipped ROM: the one .gba inside (else the one 16 MiB file) is extracted into dest_dir. Returns the path, or raises ValueError."""
    from core.constants import ROM_SIZE
    with zipfile.ZipFile(path) as z:
        files = [i for i in z.infolist() if not i.is_dir()]
        pick = [i for i in files if i.filename.lower().endswith(".gba")] or [i for i in files if i.file_size == ROM_SIZE]
        if len(pick) != 1:
            raise ValueError("The zip must hold exactly one .gba file (it holds %d)." % len(pick))
        dest = os.path.join(dest_dir, "rom.gba")
        with z.open(pick[0]) as src, open(dest, "wb") as out:
            while True:
                chunk = src.read(1 << 20)
                if not chunk:
                    return dest
                out.write(chunk)


def prepare(a):
    """Headless build for RecompLauncher. stdout carries only `progress` and ERROR lines (the full log goes to the builder's log file,
    whose path is printed on failure). Exit codes: 0 done, 1 build failed, 2 bad ROM or options, 3 source, 4 downloads not allowed."""
    game = a.game
    tmp = None
    try:
        if game and game.lower().endswith(".zip") and zipfile.is_zipfile(game):
            print(progress_line(0, "Reading your zipped ROM"), flush=True)
            tmp = tempfile.mkdtemp(prefix="bb-rom-")
            try:
                game = rom_from_zip(game, tmp)
            except (ValueError, OSError, zipfile.BadZipFile) as e:
                print("ERROR: " + str(e))
                return 2
        v = validate_rom(game) if game else {"ok": False, "message": "No game file given (--game FILE)."}
        if not v["ok"]:
            print("ERROR: " + v["message"])
            return 2
        if not a.out:
            print("ERROR: --out DIR is missing.")
            return 2
        print(progress_line(0, v["message"]), flush=True)
        b, _ = make_builder(a.repo, False)
        target = a.target or ("windows" if b.host["kind"] == "windows" else "linux32")
        need = sum(r["mb"] for r in b.plan(target))
        if need and not a.allow_downloads:
            print("ERROR: the build needs downloads (about %d MB) and --allow-downloads was not given." % need)
            return 4
        opts = {"targets": [target], "rom": game, "jobs": a.jobs, "low_priority": a.low, "dir": a.out, "container": a.container,
                "parts_dir": a.parts_dir}
        try:
            label, stages, steps, meta = b.plan_build(opts)
        except ValueError as e:
            print("ERROR: " + str(e))
            return 2
        meta["container"] = a.container
        if meta.get("parts") != meta.get("dir"):
            print(progress_line(0, parts_note(meta["dir"], meta["parts"])), flush=True)
        state = {"done": None, "stage": None}

        def on_event(ev):
            if ev["type"] == "stage":
                state["stage"] = ev["stage"]
                print(progress_line(stage_fraction(stages, ev["stage"]), STAGE_TEXT.get(ev["stage"], ev["stage"])), flush=True)
            elif ev["type"] == "state":
                state["done"] = ev

        t = b.start(label, stages, steps, meta, "build", on_event)
        last = time.time()
        try:
            while not state["done"]:
                time.sleep(0.5)
                if time.time() - last > 30 and state["stage"]:       # a quiet compile still shows the launcher a live step
                    last = time.time()
                    print(progress_line(stage_fraction(stages, state["stage"]), STAGE_TEXT.get(state["stage"], state["stage"])), flush=True)
        except KeyboardInterrupt:
            b.cancel()
            while not state["done"]:
                time.sleep(0.5)
        if state["done"]["state"] != "done":
            print("ERROR: the build %s at step '%s'. Log: %s" % (state["done"]["state"], t.final.get("step") or t.cur_step, t.logfile))
            for h in t.hints:
                print("ERROR: %s %s" % (h["title"], h["fix"]))
            return 1
        with open(os.path.join(a.out, PREPARE_MARKER), "w", encoding="utf-8") as f:
            f.write("Built by the Bravely Emerald builder from your own ROM.\n")
        print(progress_line(1, "The game is ready"), flush=True)
        return 0
    except SourceError as e:
        print("ERROR: " + str(e))
        return 3
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)


def main(argv=None):
    setup_stdout()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("checks", "build", "source", "plan"):
        p = sub.add_parser(name)
        p.add_argument("--repo", default=None)
        p.add_argument("--dry-run", action="store_true")
    sub.choices["source"].add_argument("--force", action="store_true")
    sub.choices["plan"].add_argument("--target", default=None, choices=["windows", "linux32"])
    sub.choices["plan"].add_argument("--json", action="store_true")
    bp = sub.choices["build"]
    bp.add_argument("--target", default=None, choices=["windows", "linux32"])
    bp.add_argument("--rom", "--game", dest="rom", default="", help="the ROM (--game is the same option, as RecompLauncher calls it)")
    bp.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    bp.add_argument("--low", action="store_true", help="low priority")
    bp.add_argument("--out", default="", help="install folder (the game is copied there)")
    bp.add_argument("--parts-dir", default="", help="Linux: folder without spaces for the build parts")
    bp.add_argument("--container", default="emerald-build")
    bp.add_argument("--run-test", action="store_true")
    pp = sub.add_parser("prepare", help="build without a window for RecompLauncher (prints progress lines)")
    pp.add_argument("--game", default="", help="your Pokemon Emerald ROM (.gba, or a .zip holding it)")
    pp.add_argument("--out", default="", help="folder the game is built into")
    pp.add_argument("--parts-dir", default="", help="Linux: folder without spaces for the build parts")
    pp.add_argument("--target", default=None, choices=["windows", "linux32"])
    pp.add_argument("--repo", default=None)
    pp.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    pp.add_argument("--low", action="store_true", help="low priority")
    pp.add_argument("--container", default="emerald-build")
    pp.add_argument("--allow-downloads", action="store_true", help="the player agreed to the downloads (build tools, game files)")
    sub.add_parser("selftest")
    r = sub.add_parser("rom")
    r.add_argument("file")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "rom":
            v = validate_rom(a.file)
            print(json.dumps(v, indent=2))
            return 0 if v["ok"] else 1
        if a.cmd == "selftest":
            return selftest()
        if a.cmd == "prepare":
            return prepare(a)
        if a.cmd == "source":
            make_builder(a.repo, a.dry_run, a.force)
            return 0
        b, _ = make_builder(a.repo, a.dry_run)
        if a.cmd == "plan":
            if a.json:
                print(json.dumps(b.plan(a.target), indent=1))
            else:
                print(b.plan_text(a.target))
            return 0
        if a.cmd == "checks":
            for c in b.checks():
                print("%-5s %-10s %s" % (c["status"], c["id"], c["detail"]))
            return 0
        target = a.target or ("windows" if b.host["kind"] == "windows" else "linux32")
        opts = {"targets": [target], "rom": a.rom, "jobs": a.jobs, "low_priority": a.low, "dir": a.out, "parts_dir": a.parts_dir,
                "container": a.container, "run_test": a.run_test}
        try:
            label, stages, steps, meta = b.plan_build(opts)
        except ValueError as e:
            print("ERROR:", e)
            return 2
        meta["container"] = a.container
        print("What this build will do about what is already on this PC:", flush=True)
        print(b.plan_text(target), flush=True)
        return run_task(b, label, stages, steps, meta)[0]
    except SourceError as e:
        print("ERROR:", e)
        return 3


if __name__ == "__main__":
    sys.exit(main())
