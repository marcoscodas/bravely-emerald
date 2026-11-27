"""The app never ends silently: crash hooks, one instance, odd inputs, a killed child, a full disk, no internet, and the report that
can be pasted into an issue."""
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-robust-"))

from core import Builder, crash, report, validate_rom      # noqa: E402
from core.instance import InstanceLock                      # noqa: E402
from core.tasks import Step                                 # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))


def wait(task, secs=60):
    for _ in range(int(secs * 10)):
        if task.state != "running":
            return
        time.sleep(0.1)


class CrashHookTests(unittest.TestCase):
    def setUp(self):
        self.home = tempfile.mkdtemp(prefix="bb-crash-")
        self.env = mock.patch.dict(os.environ, {"BRAVELY_BUILDER_HOME": self.home})
        self.env.start()
        self.got = []
        self.blocker = os.path.join(self.home, "afile.txt", "sub")        # a folder below a plain file cannot be created
        open(os.path.join(self.home, "afile.txt"), "w").close()
        self.old = (sys.excepthook, threading.excepthook, sys.unraisablehook)
        crash.install(lambda text, path: self.got.append((text, path)))

    def tearDown(self):
        sys.excepthook, threading.excepthook, sys.unraisablehook = self.old
        crash._state["callback"] = None
        self.env.stop()

    def test_worker_thread_exception_is_logged_and_shown(self):
        def boom():
            raise RuntimeError("worker blew up")
        t = threading.Thread(target=boom, name="w1")
        t.start()
        t.join()
        self.assertEqual(len(self.got), 1)
        self.assertIn("worker blew up", self.got[0][0])
        with open(os.path.join(self.home, "crash.log"), encoding="utf-8") as f:
            self.assertIn("worker blew up", f.read())

    def test_main_thread_hook(self):
        try:
            raise ValueError("main blew up")
        except ValueError:
            sys.excepthook(*sys.exc_info())
        self.assertIn("main blew up", self.got[0][0])

    def test_guarded_wrapper(self):
        crash.guarded(lambda: 1 / 0, "x")()
        self.assertIn("ZeroDivisionError", self.got[0][0])

    def test_unwritable_home_falls_back_to_temp(self):
        with mock.patch.dict(os.environ, {"BRAVELY_BUILDER_HOME": self.blocker}):
            path = crash.write_crash("fallback test")
        self.assertTrue(path and os.path.isfile(path))

    def test_error_in_the_callback_does_not_loop(self):
        crash._state["callback"] = lambda t, p: 1 / 0
        try:
            raise ValueError("x")
        except ValueError:
            sys.excepthook(*sys.exc_info())          # must return normally
        with open(os.path.join(self.home, "crash.log"), encoding="utf-8") as f:
            self.assertIn("the crash dialog itself failed", f.read())


class InstanceTests(unittest.TestCase):
    def test_second_copy_is_refused_and_lock_is_released(self):
        d = tempfile.mkdtemp(prefix="bb-lock-")
        a, b = InstanceLock(d), InstanceLock(d)
        self.assertTrue(a.acquire())
        self.assertFalse(b.acquire())
        a.release()
        self.assertTrue(b.acquire())
        b.release()

    @staticmethod
    def blocked():
        f = os.path.join(tempfile.mkdtemp(), "afile.txt")
        open(f, "w").close()
        return os.path.join(f, "sub")

    def test_unwritable_home_does_not_block(self):
        self.assertTrue(InstanceLock(os.path.join(tempfile.mkdtemp(), "\0x")).acquire())


class OddInputTests(unittest.TestCase):
    def test_rom_inputs(self):
        d = tempfile.mkdtemp(prefix="bb-rom-")
        empty = os.path.join(d, "zero.gba")
        open(empty, "wb").close()
        cases = {"": "empty", d: "folder", empty: "0 bytes", os.path.join("Z:\\", "nope", "e.gba") if os.name == "nt" else "/nonexistent/e.gba": "no drive",
                 "relative.gba": "relative", "\\\\no-such-server-xyz\\share\\e.gba" if os.name == "nt" else "//no-such/e.gba": "network"}
        for path, why in cases.items():
            with self.subTest(why):
                t0 = time.time()
                v = validate_rom(path)
                self.assertFalse(v["ok"], why)
                self.assertTrue(v["message"])
                self.assertLess(time.time() - t0, 30, "must not hang")

    def test_unreadable_rom(self):
        d = tempfile.mkdtemp(prefix="bb-rom-")
        p = os.path.join(d, "locked.gba")
        with open(p, "wb") as f:
            f.write(b"\0" * (16 * 1024 * 1024))
        with mock.patch("builtins.open", side_effect=PermissionError("denied")):
            v = validate_rom(p)
        self.assertFalse(v["ok"])
        self.assertIn("Cannot read", v["message"])


class RunnerTests(unittest.TestCase):
    def run_step(self, code, kind="test"):
        b = Builder(REPO)
        step = Step("py", "compile", [sys.executable, "-c", code])
        t = b.start("t", ["compile"], [step], {"logdir": tempfile.mkdtemp(), "low": True}, kind)
        wait(t)
        return t

    def test_killed_child_ends_as_failed_with_footer(self):
        t = self.run_step("import os,signal,sys; print('working', flush=True); os._exit(137)")
        self.assertEqual(t.state, "failed")
        tail = [e["text"] for e in t.events if e["type"] == "log"][-3:]
        self.assertTrue(any(x.startswith("FAILED: py (exit code 137)") for x in tail), tail)
        self.assertTrue(any(x.startswith("Probable cause:") for x in tail))

    def test_stderr_is_in_the_log(self):
        t = self.run_step("import sys; sys.stderr.write('to stderr\\n'); sys.exit(3)")
        self.assertTrue(any("to stderr" in e.get("text", "") for e in t.events))

    def test_missing_program_is_a_failed_task_not_a_crash(self):
        b = Builder(REPO)
        t = b.start("t", ["compile"], [Step("nope", "compile", ["definitely-not-a-program-xyz"])], {"logdir": tempfile.mkdtemp()}, "test")
        wait(t)
        self.assertEqual(t.state, "failed")
        self.assertIn("FAILED: nope", "\n".join(e.get("text", "") for e in t.events))

    def test_read_only_install_folder(self):
        from core.builder import install_windows_result
        repo = tempfile.mkdtemp(prefix="bb-ro-")
        os.makedirs(os.path.join(repo, "dist-win"))
        open(os.path.join(repo, "dist-win", "pokeemerald.exe"), "w").close()
        b = Builder(REPO)
        step = Step("copy", "package", [], func=lambda task: install_windows_result(task, repo, os.path.join(tempfile.mkdtemp(), "out")))
        with mock.patch("shutil.copy2", side_effect=PermissionError("read-only")):
            t = b.start("t", ["package"], [step], {"logdir": tempfile.mkdtemp()}, "test")
            wait(t)
        self.assertEqual(t.state, "failed")
        self.assertIn("read-only", "\n".join(e.get("text", "") for e in t.events))


class PreconditionTests(unittest.TestCase):
    def builder(self):
        os.environ["BRAVELY_FAKE_HOST"] = "windows"
        self.addCleanup(os.environ.pop, "BRAVELY_FAKE_HOST", None)
        return Builder(REPO)

    def test_full_disk_blocks_with_a_plain_message(self):
        b = self.builder()
        with mock.patch("core.builder.free_gb", return_value=0.2), mock.patch("core.builder.network_ok", return_value=True), \
             mock.patch.object(type(b.probe), "msys", lambda self: ("C:\\msys64", "x")), \
             mock.patch.object(type(b.probe), "packages_missing", lambda self: ([], None)):
            items = b.checks()
        disk = [c for c in items if c["id"] == "disk"][0]
        self.assertEqual(disk["status"], "bad")
        self.assertTrue(disk["blocking"])
        self.assertIn("at least", disk["detail"])

    def test_no_internet_blocks_with_a_plain_message(self):
        b = self.builder()
        with mock.patch("core.builder.free_gb", return_value=50), mock.patch("core.builder.network_ok", return_value=False), \
             mock.patch.object(type(b.probe), "msys", lambda self: ("C:\\msys64", "x")), \
             mock.patch.object(type(b.probe), "packages_missing", lambda self: ([], None)):
            items = b.checks()
        net = [c for c in items if c["id"] == "net"][0]
        self.assertEqual(net["status"], "bad")
        self.assertIn("not reachable", net["detail"])

    def test_msys2_installer_is_one_pinned_release_with_a_pinned_checksum(self):
        # releases/latest on GitHub became MSYS2's nightly build, which publishes no .sha256 file
        from core import constants
        self.assertNotIn("/latest/", constants.MSYS_INSTALLER_URL)
        self.assertRegex(constants.MSYS_INSTALLER_URL, r"/releases/download/\d{4}-\d{2}-\d{2}/")
        self.assertTrue(constants.MSYS_INSTALLER_URL.endswith("/" + constants.MSYS_INSTALLER_FILE))
        self.assertRegex(constants.MSYS_INSTALLER_SHA256, r"^[0-9a-f]{64}$")

    def test_msys2_download_is_always_checked_against_the_pinned_checksum(self):
        from core import constants
        b = Builder(REPO)
        asked = []

        class T:
            cancel_requested = False

            def log(self, s):
                pass

            def set_stage(self, s):
                pass
        with mock.patch.object(b, "_download", lambda task, dest, expected: asked.append(expected) or False), \
             mock.patch("urllib.request.urlopen", side_effect=OSError("offline")), \
             mock.patch.dict(os.environ, {"BRAVELY_MSYS2_INSTALLER": ""}), \
             mock.patch("tempfile.gettempdir", lambda d=tempfile.mkdtemp(): d):
            self.assertFalse(b._run_msys2_install(T()))
        self.assertEqual(asked, [constants.MSYS_INSTALLER_SHA256])

    def test_msys2_download_failure_is_a_logged_failure(self):
        b = Builder(REPO)
        lines = []

        class T:
            cancel_requested = False

            def log(self, s):
                lines.append(s)

            def set_stage(self, s):
                pass
        with mock.patch("urllib.request.urlopen", side_effect=OSError("no route to host")):
            ok = b._download(T(), os.path.join(tempfile.mkdtemp(), "i.exe"), None)
        self.assertFalse(ok)
        self.assertTrue(any("download failed" in x and "no route" in x for x in lines))


class ReportTests(unittest.TestCase):
    def test_report_block_has_header_log_and_footer(self):
        os.environ["BRAVELY_FAKE_HOST"] = "windows"
        self.addCleanup(os.environ.pop, "BRAVELY_FAKE_HOST", None)
        b = Builder(REPO)
        b.manifest = {"commit": "a" * 40, "date": "2026-11-15T00:00:00"}
        b.probe = mock.Mock(msys=lambda: ("C:\\msys64", None), packages_missing=lambda: ([], None), deps=lambda: "missing",
                            tools=lambda: False, assets=lambda: False, build_dir=lambda: "missing")
        step = Step("py", "compile", [sys.executable, "-c", "import sys; print('out'); sys.stderr.write('err\\n'); sys.exit(2)"])
        t = b.start("Windows build", ["compile"], [step], {"logdir": tempfile.mkdtemp(), "low": True}, "build")
        wait(t, 120)
        text = report.full_report(t, b, b.manifest)
        self.assertTrue(text.startswith("```"))
        for needle in ("Bravely Emerald Builder", "aaaaaaaaaaaa", "CPU:", "Plan (what was found", "out", "err",
                       "FAILED: py (exit code 2)", "Probable cause:"):
            self.assertIn(needle, text)
        self.assertLess(text.index("Plan (what"), text.index("FAILED: py"))
        with open(t.logfile, encoding="utf-8") as f:
            disk = f.read()
        self.assertIn("FAILED: py (exit code 2)", disk)
        self.assertIn("Bravely Emerald Builder", disk)


if __name__ == "__main__":
    unittest.main()
