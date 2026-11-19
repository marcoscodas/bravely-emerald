"""Tests of the UI-agnostic core. Run: python -m unittest discover -s tools/builder/tests -v
No ROM, network or build tools are needed (a synthetic ROM header is made in a temp folder)."""
import io
import os
import subprocess
import sys
import tempfile
import time
import unittest
from datetime import datetime
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-settings-"))

from core import Builder, explain, validate_rom, clean_path, load_settings, save_settings    # noqa: E402
from core.errors import container_hint                                                      # noqa: E402
from core.host import detect_host                                                           # noqa: E402
from core.probe import linux_tool_version, podman_logs                                       # noqa: E402
from core.repo import is_repo, guess_repo                                                   # noqa: E402
from core.tasks import Step, Task                                                           # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))


def make_rom(path, title=b"POKEMON EMER", code=b"BPEE", size=16 * 1024 * 1024, fix_sum=True):
    buf = bytearray(size)
    buf[0xA0:0xA0 + len(title)] = title
    buf[0xAC:0xB0] = code
    if fix_sum and size > 0xBD:
        buf[0xBD] = (0 - (sum(buf[0xA0:0xBD]) + 25)) & 255
    with open(path, "wb") as f:
        f.write(buf)


class RomTests(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp(prefix="bb-rom-")

    def p(self, name):
        return os.path.join(self.d, name)

    def test_good(self):
        make_rom(self.p("a.gba"))
        r = validate_rom(self.p("a.gba"))
        self.assertTrue(r["ok"], r)
        self.assertEqual(r["code"], "BPEE")
        self.assertFalse(r["sha1_match"])

    def test_other_language_code(self):
        make_rom(self.p("a.gba"), code=b"BPED")
        self.assertTrue(validate_rom(self.p("a.gba"))["ok"])

    def test_truncated(self):
        make_rom(self.p("a.gba"), size=1024 * 1024)
        r = validate_rom(self.p("a.gba"))
        self.assertFalse(r["ok"])
        self.assertIn("size", r["message"])

    def test_wrong_game(self):
        make_rom(self.p("a.gba"), title=b"POKEMON RUBY", code=b"AXVE")
        self.assertIn("title", validate_rom(self.p("a.gba"))["message"])

    def test_wrong_code(self):
        make_rom(self.p("a.gba"), code=b"AXVE")
        self.assertIn("game code", validate_rom(self.p("a.gba"))["message"])

    def test_modified_header(self):
        make_rom(self.p("a.gba"), fix_sum=False)
        self.assertIn("checksum", validate_rom(self.p("a.gba"))["message"])

    def test_missing_and_relative(self):
        self.assertFalse(validate_rom(self.p("nope.gba"))["ok"])
        self.assertFalse(validate_rom("rom.gba")["ok"])

    def test_not_a_rom_text_file(self):
        with open(self.p("t.gba"), "w") as f:
            f.write("hello")
        self.assertFalse(validate_rom(self.p("t.gba"))["ok"])

    @unittest.skipUnless(os.environ.get("BRAVELY_TEST_ROM"), "set BRAVELY_TEST_ROM to a real Emerald dump")
    def test_real_rom(self):
        self.assertTrue(validate_rom(os.environ["BRAVELY_TEST_ROM"])["ok"])


class ValidationTests(unittest.TestCase):
    def test_paths(self):
        root = os.path.abspath(os.sep)
        good = os.path.join(root, "a", "b")
        self.assertEqual(clean_path(good), os.path.normpath(good))
        for bad in ("", "relative/dir", os.path.join(root, "a b"), os.path.join(root, "a;rm"), os.path.join(root, "a$(x)"),
                    os.path.join(root, "a\nb"), os.path.join(root, "a`x`"), "x" * 500, None, 5):
            with self.assertRaises(ValueError, msg=repr(bad)):
                clean_path(bad)
        self.assertTrue(clean_path(os.path.join(root, "a b"), no_space=False))


class HostTests(unittest.TestCase):
    def test_fake_hosts(self):
        for kind in ("windows", "steamos", "linux"):
            os.environ["BRAVELY_FAKE_HOST"] = kind
            try:
                self.assertEqual(detect_host()["kind"], kind)
            finally:
                del os.environ["BRAVELY_FAKE_HOST"]

    def test_real_host_known(self):
        self.assertIn(detect_host()["kind"], ("windows", "linux", "steamos", "other"))


class ErrorTableTests(unittest.TestCase):
    CASES = {
        "No protocol specified": "X display",
        "ERROR: Python 3 with Pillow is needed": "Pillow",
        "fatal: png.h: No such file": "libpng",
        "ERROR: not a Pokemon Emerald dump (size 5 bytes, expected 16 MB)": "ROM was rejected",
        "ERROR: no internet (github.com unreachable)": "download failed",
        "error: failed retrieving file 'msys.db': Connection timed out": "download failed",
        "ERROR: mingw-w64-i686-toolchain missing: pacman -S mingw-w64-i686-toolchain": "toolchain packages",
        "ERROR: the repository path contains a space; move it": "contains a space",
        "ERROR: less than 4000 MB free in /x": "disk space",
        "fatal: could not read Username for 'https://github.com'": "private",
        "sudo: a password is required": "password",
    }

    def test_each_known(self):
        for line, expect in self.CASES.items():
            hits = explain(["noise", line])
            self.assertTrue(any(expect.lower() in (h["title"] + h["fix"]).lower() for h in hits), (line, hits))

    def test_unknown_is_empty(self):
        self.assertEqual(explain(["all fine", "done"]), [])


class LinuxDiagnosticsTests(unittest.TestCase):
    OPENSSL = ":: installing openssl (4.1.0-1)\nbreaks dependency 'libcrypto.so=3-64' required by pacman"
    CHPASSWD = "chpasswd: (line 1, user private-user)\n\ninvalid password hash"
    TIMEOUT = "error: failed retrieving file 'core.db' from mirror: Connection timed out"
    PERMISSIONS = "Error: crun: open /home/private-user/containers/overlay/x/merged: Permission denied"
    GENERIC = "\x1b[31mInstalling basic packages... Error: An error occurred\x1b[0m"
    START_CHECK = "    checking that the build container starts [100.123456]"
    ENTER_FAILED = "ERROR: the container does not start. See docs/BUILD_DECK.md, Troubleshooting. [101.234567]"
    CONTAINER_ID = "1a2b3c4d5e6f"

    def test_specific_causes_take_priority(self):
        for error, expected in ((self.OPENSSL, "OpenSSL dependency conflict"),
                                ("error: breaks dependency 'libcrypto.so=3-64' required by pacman", "OpenSSL dependency conflict"),
                                (self.CHPASSWD, "invalid password hash"),
                                ("chpasswd: invalid password hash", "invalid password hash"),
                                (self.TIMEOUT, "network operation timed out"),
                                (self.PERMISSIONS, "container permission error")):
            with self.subTest(expected=expected, error=error):
                extra = [self.TIMEOUT] if error in (self.OPENSSL, self.CHPASSWD) else []
                hit = container_hint(extra + [self.GENERIC, error, self.ENTER_FAILED])
                self.assertIn(expected, hit["title"])
                self.assertNotIn("private-user", str(hit))

    def test_generic_messages_are_not_evidence(self):
        self.assertIsNone(container_hint([self.GENERIC, self.ENTER_FAILED,
                                         "Ensuring user's access... Error: An error occurred",
                                         "ERROR: distrobox create failed 3 times",
                                         "looking at containers/overlay",
                                         "+ printf 'chpasswd: invalid password hash'"]))

    def _failed_build(self, output, side_effect, host="linux"):
        b = Builder(REPO)
        b.host = {"kind": host}
        step = Step("tools/deck/build_deck.sh", "prereq", ["bash", "tools/deck/build_deck.sh"],
                    stage_rules=[(r"^=== 2/6", "container")])
        proc = mock.Mock(stdout=io.BytesIO(output.encode()), returncode=1)
        with tempfile.TemporaryDirectory() as directory:
            log = os.path.join(directory, "build.log")
            task = Task("build", "Build", ["prereq", "container"], logfile=log)
            task.meta = {"target": "linux32", "container": "custom-build"}
            with mock.patch.object(b, "_spawn", return_value=(proc, step.argv)), \
                    mock.patch("core.builder.report.header", return_value=["Builder"]), \
                    mock.patch("core.probe.subprocess.run", side_effect=side_effect) as run:
                b._run(task, [step])
            with open(log, encoding="utf-8") as f:
                log_text = f.read()
        self.assertEqual(task.state, "failed")
        self.assertEqual(task.final.get("code"), 1)
        cause = task.hints[0]["title"] if task.hints else "unknown"
        self.assertIn("Probable cause: " + cause, log_text)
        return task, log_text, run

    def test_failed_enter_recovers_only_a_diagnosis_from_podman(self):
        def probe(argv, **kwargs):
            if argv == ["distrobox", "--version"]:
                return subprocess.CompletedProcess(argv, 0, "distrobox: 1.7.0\n")
            if argv == ["podman", "--version"]:
                return subprocess.CompletedProcess(argv, 0, "podman version 4.9.3\n")
            if argv == ["distrobox", "list"]:
                listing = "ID | NAME | STATUS | IMAGE\n%s | custom-build | Exited | archlinux\n" % self.CONTAINER_ID
                return subprocess.CompletedProcess(argv, 0, listing)
            messages = ["+ printf PRIVATE_PASSWORD"] + self.CHPASSWD.splitlines()
            log = "\n".join("1970-01-01T00:01:40.500000000Z " + line for line in messages)
            return subprocess.CompletedProcess(argv, 0, log)

        output = "\n".join(["=== 2/6 Build container ===", self.TIMEOUT,
                            self.START_CHECK, self.GENERIC,
                            "+ printf PRIVATE_TRACE", self.ENTER_FAILED])
        task, log, run = self._failed_build(output, probe)
        self.assertIn("invalid password hash", task.hints[0]["title"])
        self.assertIn("Distrobox: 1.7.0\nPodman: 4.9.3", log)
        self.assertNotIn("PRIVATE_", log)
        self.assertNotIn("private-user", log)
        args, kwargs = run.call_args
        self.assertEqual(args[0][:3], ["podman", "logs", "--since"])
        self.assertEqual(datetime.fromisoformat(args[0][3]).timestamp(), 100.123456)
        self.assertEqual(args[0][4], "--until")
        self.assertEqual(datetime.fromisoformat(args[0][5]).timestamp(), 101.234567)
        self.assertEqual(args[0][6:], ["--timestamps", "--tail", "100", "--", self.CONTAINER_ID])
        self.assertEqual(kwargs["timeout"], 3)
        self.assertEqual(kwargs["stderr"], subprocess.STDOUT)

    def test_missing_diagnostics_keep_generic_failure(self):
        output = "\n".join(["=== 2/6 Build container ===", self.START_CHECK, self.GENERIC, self.ENTER_FAILED])
        for exception in (FileNotFoundError(), subprocess.TimeoutExpired("podman", 3)):
            with self.subTest(exception=type(exception)):
                task, _, _ = self._failed_build(output, exception)
                self.assertIn("unknown reason", task.hints[0]["title"])
                self.assertIn("podman logs --tail 100 custom-build", task.hints[0]["fix"])

    def test_distrobox_historical_dump_is_neither_shown_nor_classified(self):
        output = "\n".join(["=== 2/6 Build container ===", self.TIMEOUT, self.START_CHECK,
                            "\x1b[31m Error: could not start entrypoint.",
                            self.OPENSSL, "+ printf PRIVATE_PASSWORD", self.ENTER_FAILED])
        task, log, run = self._failed_build(output, FileNotFoundError())
        self.assertIn("unknown reason", task.hints[0]["title"])
        self.assertNotIn("PRIVATE_PASSWORD", log)
        self.assertNotIn("libcrypto", log)
        self.assertEqual(run.call_args.args[0], ["distrobox", "list"])

    def test_entrypoint_message_during_create_leaves_final_error_visible(self):
        create_error = "ERROR: distrobox create failed 3 times"
        output = "\n".join(["=== 2/6 Build container ===", "Error: could not start entrypoint.", create_error])
        task, log, run = self._failed_build(output, FileNotFoundError())
        self.assertIn(create_error, log)
        self.assertIn("unknown reason", task.hints[0]["title"])
        self.assertEqual([call.args[0][-1] for call in run.call_args_list], ["--version", "--version"])

    def test_other_failure_phases_do_not_read_podman_logs(self):
        for error in ("ERROR: distrobox create failed 3 times", "ERROR: package install failed"):
            with self.subTest(error=error):
                _, _, run = self._failed_build(
                    "=== 2/6 Build container ===\n" + self.PERMISSIONS + "\n" + error, FileNotFoundError())
                self.assertEqual([call.args[0][-1] for call in run.call_args_list], ["--version", "--version"])

    def test_steamos_does_not_run_extra_commands(self):
        task, _, run = self._failed_build(
            "=== 2/6 Build container ===\n" + self.PERMISSIONS + "\n" + self.ENTER_FAILED,
            AssertionError("unexpected probe"), host="steamos")
        self.assertIn("container permission error", task.hints[0]["title"])
        run.assert_not_called()

    def test_probe_failures_cannot_supply_a_diagnosis(self):
        with mock.patch("core.probe.subprocess.run", side_effect=[
                subprocess.CompletedProcess(["distrobox", "list"], 0, "%s | custom-build | Exited" % self.CONTAINER_ID),
                subprocess.CompletedProcess(["podman"], 125, self.PERMISSIONS),
                subprocess.CompletedProcess(["podman"], 125, self.PERMISSIONS)]) as run:
            self.assertEqual(podman_logs("custom-build", 0, 1), [])
            self.assertIsNone(linux_tool_version("podman"))
            self.assertEqual(podman_logs("--latest", 0, 1), [])
            self.assertEqual(run.call_count, 3)

    def test_unidentified_distrobox_container_skips_podman(self):
        listing = "ID | NAME | STATUS\n%s | unrelated-build | Exited\n" % self.CONTAINER_ID
        with mock.patch("core.probe.subprocess.run", return_value=subprocess.CompletedProcess(
                ["distrobox", "list"], 0, listing)) as run:
            self.assertEqual(podman_logs("custom-build", 0, 1), [])
            self.assertEqual(run.call_count, 1)
            self.assertEqual(run.call_args.args[0], ["distrobox", "list"])

    def test_colored_distrobox_listing_preserves_container_id(self):
        listing = ("ID | NAME | STATUS\n"
                   "\x1b[33m%s | \x1b[1mcustom-build\x1b[0m | Exited\x1b[0m\n" % self.CONTAINER_ID)
        message = "chpasswd: invalid password hash"
        log = "1970-01-01T00:00:00.500000000Z " + message
        with mock.patch("core.probe.subprocess.run", side_effect=[
                subprocess.CompletedProcess(["distrobox", "list"], 0, listing),
                subprocess.CompletedProcess(["podman", "logs"], 0, log)]) as run:
            self.assertEqual(podman_logs("custom-build", 0, 1), [message])
            self.assertEqual(run.call_args.args[0][-1], self.CONTAINER_ID)


class SettingsTests(unittest.TestCase):
    def test_roundtrip_and_whitelist(self):
        save_settings({"jobs": 3, "rom": "/x/y.gba", "evil": "x", "low_priority": True, "container": 5})
        s = load_settings()
        self.assertEqual(s["jobs"], 3)
        self.assertEqual(s["rom"], "/x/y.gba")
        self.assertNotIn("evil", s)
        self.assertNotIn("container", s)        # wrong type is dropped
        save_settings({"jobs": True})            # bool is not an int
        self.assertEqual(load_settings()["jobs"], 3)


class RepoTests(unittest.TestCase):
    def test_this_checkout_is_a_repo(self):
        self.assertTrue(is_repo(REPO))
        self.assertEqual(os.path.normcase(guess_repo()), os.path.normcase(REPO))

    def test_empty_dir_is_not(self):
        self.assertFalse(is_repo(tempfile.mkdtemp()))


class PlanTests(unittest.TestCase):
    """Command construction in dry-run mode, for each simulated host."""

    def _builder(self, kind):
        os.environ["BRAVELY_FAKE_HOST"] = kind
        self.addCleanup(os.environ.pop, "BRAVELY_FAKE_HOST", None)
        return Builder(REPO, dry_run=True)

    def test_targets_per_host(self):
        for kind, win, lin in (("windows", True, False), ("steamos", False, True), ("linux", False, True)):
            t = {x["id"]: x["available"] for x in self._builder(kind).targets()}
            self.assertEqual((t["windows"], t["linux32"]), (win, lin), kind)

    def test_cross_target_refused(self):
        with self.assertRaises(ValueError):
            self._builder("steamos").plan_build({"targets": ["windows"], "jobs": 2, "rom": ""})

    def test_one_target_only(self):
        with self.assertRaises(ValueError):
            self._builder("steamos").plan_build({"targets": ["windows", "linux32"], "jobs": 2})

    def test_linux_command(self):
        b = self._builder("steamos")
        if b.have_assets():
            self.skipTest("this checkout already has game files")
        d = tempfile.mkdtemp()
        rom = os.path.join(d, "e.gba")
        make_rom(rom)
        label, stages, steps, meta = b.plan_build({"targets": ["linux32"], "jobs": 2, "rom": rom, "dir": os.path.join(d, "out"),
                                                   "container": "ui-test", "run_test": False})
        argv = steps[0].argv
        self.assertEqual(argv[0], "bash")
        self.assertTrue(argv[1].replace("\\", "/").endswith("tools/deck/build_deck.sh"))
        for flag in ("--rom", "--dir", "--jobs", "--container", "--no-run"):
            self.assertIn(flag, argv)
        self.assertEqual(argv[argv.index("--jobs") + 1], "2")
        self.assertEqual(argv[argv.index("--container") + 1], "ui-test")
        self.assertEqual(stages[0], "prereq")

    def test_linux_rejects_bad_input(self):
        b = self._builder("linux")
        base = {"targets": ["linux32"], "jobs": 2, "rom": "", "dir": os.path.abspath(os.sep) + "x"}
        for patch in ({"jobs": 0}, {"jobs": "4"}, {"jobs": True}, {"container": "a b"}, {"container": "x;rm -rf /"}, {"dir": "rel"}):
            with self.assertRaises(ValueError, msg=str(patch)):
                b.plan_build(dict(base, **patch))

    def test_windows_steps(self):
        b = self._builder("windows")
        if b.have_assets():
            steps = [s for s in b.plan_build({"targets": ["windows"], "jobs": 3})[2] if s.stage != "prereq"]
            self.assertEqual([s.stage for s in steps], ["tools", "package", "package"])
        else:
            rom = os.path.join(tempfile.mkdtemp(), "e.gba")
            make_rom(rom)
            steps = [s for s in b.plan_build({"targets": ["windows"], "jobs": 3, "rom": rom})[2] if s.stage != "prereq"]
            self.assertEqual([s.stage for s in steps], ["files", "tools", "assets", "files", "tools", "package", "package"])
            self.assertIn("FETCH_NO_CONVERT=1", steps[0].argv[2])
            self.assertEqual(steps[0].env["MSYSTEM"], "MINGW64")
            self.assertIn(rom, steps[0].argv)               # the ROM path is its own argument ...
            self.assertNotIn(rom, steps[0].argv[2])         # ... never spliced into the shell text
            tol = steps[0].tolerate
            self.assertTrue(tol(["=== Copying 5 files ===", "=== Generating the port's files ==="]))
            self.assertFalse(tol(["ERROR: not a Pokemon Emerald dump"]))
        self.assertIn("OMP_NUM_THREADS", steps[-3].argv[2])
        self.assertTrue(steps[-1].func)

    def test_dry_run_task_finishes(self):
        b = self._builder("steamos")
        rom = os.path.join(tempfile.mkdtemp(), "e.gba")
        make_rom(rom)
        opts = {"targets": ["linux32"], "jobs": 2, "rom": rom, "dir": os.path.join(tempfile.mkdtemp(), "o"), "container": "t"}
        label, stages, steps, meta = b.plan_build(opts)
        t = b.start(label, stages, steps, meta)
        for _ in range(100):
            if t.state != "running":
                break
            time.sleep(0.05)
        self.assertEqual(t.state, "done")
        self.assertTrue(any("[dry run]" in e.get("text", "") for e in t.events))
        self.assertIsNone(t.logfile)                       # a dry run writes no files

    def test_install_rules(self):
        with self.assertRaises(ValueError):
            self._builder("steamos").plan_install("deps")
        with self.assertRaises(ValueError):
            self._builder("windows").plan_install("msys2", consent=False)
        self.assertEqual(len(self._builder("windows").plan_install("msys2", consent=True)), 4)
        b = self._builder("windows")
        if os.path.isfile(b.msys_bash()):
            for what in ("packages", "deps"):
                label, stages, steps, meta = b.plan_install(what, True)
                self.assertTrue(steps and "logdir" in meta)


class RunnerTests(unittest.TestCase):
    """Real subprocess handling with Python as the 'script'."""

    def _run(self, code, stage_rules=None, tolerate=None, cancel_after=None):
        b = Builder(REPO)
        step = Step("py", "compile", [sys.executable, "-u", "-c", code], stage_rules=stage_rules or [], tolerate=tolerate)
        t = b.start("test", ["compile", "link"], [step], {"logdir": tempfile.mkdtemp(), "low": True}, "test")
        if cancel_after:
            time.sleep(cancel_after)
            b.cancel()
        for _ in range(300):
            if t.state != "running":
                break
            time.sleep(0.1)
        return t

    def test_success_stage_and_progress(self):
        t = self._run("print('[ 50%] Building x.o'); print('Linking C executable pokeemerald')",
                      stage_rules=[(r"Linking (C|CXX) executable", "link")])
        self.assertEqual(t.state, "done")
        types = [(e["type"], e.get("stage") or e.get("pct")) for e in t.events]
        self.assertIn(("progress", 50), types)
        self.assertIn(("stage", "link"), types)
        self.assertTrue(os.path.getsize(t.logfile) > 0)            # log saved to a file

    def test_failure_has_hint(self):
        t = self._run("import sys; print('Error: png.h: No such file'); sys.exit(3)")
        self.assertEqual(t.state, "failed")
        self.assertTrue(t.hints)

    def test_tolerated_failure(self):
        self.assertEqual(self._run("import sys; print('x'); sys.exit(2)", tolerate=lambda lines: True).state, "done")

    def test_cancel_kills_tree(self):
        child = ("import subprocess,sys,time\n"
                 "p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)'])\n"
                 "print('child',p.pid,flush=True)\ntime.sleep(60)\n")
        t = self._run(child, cancel_after=2.5)
        self.assertEqual(t.state, "cancelled")
        pid = None
        for e in t.events:
            if e["type"] == "log" and e["text"].startswith("child "):
                pid = int(e["text"].split()[1])
        self.assertIsNotNone(pid)
        time.sleep(0.5)
        if os.name == "nt":
            alive = str(pid) in subprocess.run(["tasklist", "/FI", "PID eq %d" % pid], capture_output=True, text=True).stdout
        else:
            try:
                os.kill(pid, 0)
                alive = True
            except OSError:
                alive = False
        self.assertFalse(alive, "grandchild survived the cancel")


if __name__ == "__main__":
    unittest.main()
