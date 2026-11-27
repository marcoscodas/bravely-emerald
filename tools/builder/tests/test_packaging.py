"""The frozen app must hold every module of core/ and ui/: the packaging collects them automatically, lists them in the bundle,
and refuses to produce a package whose exe cannot import them (the build that Marcos launched failed on `core.crash`)."""
import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

BUILDER = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BUILDER)


class PackagingTests(unittest.TestCase):
    def test_pyinstaller_collects_the_packages_not_a_list(self):
        src = open(os.path.join(BUILDER, "package.py"), encoding="utf-8").read()
        self.assertIn('"--collect-submodules", "core", "--collect-submodules", "ui"', src)
        self.assertNotIn("--hidden-import", src)
        self.assertEqual([ln.strip() for ln in src.splitlines()].count("verify_frozen(exe)"), 2)                # Windows and Linux packaging both verify the exe

    def test_recomplauncher_json_goes_next_to_the_program_in_both_packages(self):
        import json
        import package
        folder = tempfile.mkdtemp(prefix="bb-rl-")
        dest = package.add_launcher_file(folder)
        self.assertEqual(os.path.dirname(dest), folder)                      # top of the release folder, not in a subfolder
        self.assertEqual(os.path.basename(dest), "recomplauncher.json")
        with open(dest, encoding="utf-8") as f:
            data = json.load(f)
        self.assertEqual(data["format"], 1)
        self.assertEqual(data["repo"], "marcoscodas/bravely-emerald")
        self.assertIn("BravelyEmeraldBuilder.exe", data["prepare"]["program"])    # the names the packages ship
        self.assertIn("BravelyEmeraldBuilder", data["prepare"]["program"])
        import cli
        self.assertEqual(data["prepare"]["done"], ["{install}/game/" + cli.PREPARE_MARKER])              # the file --prepare writes
        src = open(os.path.join(BUILDER, "package.py"), encoding="utf-8").read()
        self.assertEqual([ln.strip() for ln in src.splitlines()].count("add_launcher_file(folder)"), 1)    # Windows
        self.assertEqual([ln.strip() for ln in src.splitlines()].count("add_launcher_file(app)"), 1)       # Linux

    def test_module_list_matches_the_files(self):
        import package
        names = set(package.module_names())
        for pkg in ("core", "ui"):
            for fn in os.listdir(os.path.join(BUILDER, pkg)):
                if fn.endswith(".py") and fn != "__init__.py":
                    self.assertIn("%s.%s" % (pkg, fn[:-3]), names, fn)
        for needed in ("core.crash", "core.instance", "core.probe", "core.report", "ui.main_window"):
            self.assertIn(needed, names)

    def test_imports_check_passes_on_the_source(self):
        env = dict(os.environ, BRAVELY_BUILDER_HOME=tempfile.mkdtemp(prefix="bb-imp-"), QT_QPA_PLATFORM="offscreen")
        p = subprocess.run([sys.executable, "-I", os.path.join(BUILDER, "app.py"), "--imports"], capture_output=True, text=True, timeout=120, env=env)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("IMPORTS OK", p.stdout)
        self.assertIn("OK   core.crash", p.stdout)

    def start_failure_text(self, exc, frozen):
        import app
        shown = []
        with mock.patch.object(app, "fatal_box", lambda title, text: shown.append(text)), \
             mock.patch.object(app.sys, "frozen", frozen, create=True), \
             mock.patch.dict(os.environ, {"BRAVELY_BUILDER_HOME": tempfile.mkdtemp(prefix="bb-fatal-")}):
            app.start_failed(exc)
        return shown[0]

    def test_downloaded_builder_without_its_window_files_names_the_problem(self):
        text = self.start_failure_text(ImportError("DLL load failed while importing QtCore: not found", name="PySide6.QtCore"), True)
        for part in ("BB-UI-DOWNLOAD", "DLL load failed", "empty folder", "github.com/marcoscodas/bravely-emerald/issues",
                     "Installing PySide6 yourself will not help"):
            self.assertIn(part, text)
        self.assertNotIn("pip install", text)

    def test_running_from_source_without_pyside6_gives_the_command_and_link(self):
        text = self.start_failure_text(ModuleNotFoundError("No module named 'PySide6'", name="PySide6"), False)
        for part in ("BB-UI-SOURCE", "pip install PySide6-Essentials", "https://pypi.org/project/PySide6/",
                     "releases/latest"):
            self.assertIn(part, text)

    def test_other_start_failures_keep_their_own_message(self):
        text = self.start_failure_text(ImportError("cannot import name 'crash' from 'core'", name="core"), True)
        self.assertIn("download the package again", text)
        self.assertNotIn("BB-UI", text)

    def test_the_window_import_failure_goes_through_that_message(self):
        """It used to exit with "pip install PySide6" even in the downloaded builder, where that cannot help."""
        with open(os.path.join(BUILDER, "app.py"), encoding="utf-8") as f:
            self.assertNotIn('sys.exit("PySide6 is needed', f.read())

    def test_msys2_messages_carry_their_codes(self):
        from core import Builder
        b = Builder(os.path.dirname(os.path.dirname(BUILDER)), dry_run=True)
        b.host = {"kind": "windows"}
        with mock.patch.object(b, "msys_bash", lambda: os.path.join(tempfile.mkdtemp(), "bash.exe")):
            for what in ("packages", "deps"):
                with self.assertRaises(ValueError) as c:
                    b._plan_install(what)
                for part in ("BB-MSYS2-MISSING", "https://www.msys2.org", "C:\\msys64"):
                    self.assertIn(part, str(c.exception))
        with open(os.path.join(BUILDER, "core", "builder.py"), encoding="utf-8") as f:
            self.assertIn("ERROR: [BB-MSYS2-DOWNLOAD] could not download the MSYS2 installer", f.read())

    def test_import_error_at_start_gives_a_message_not_a_traceback_dialog(self):
        import app
        shown = []
        old = app.fatal_box
        app.fatal_box = lambda title, text: shown.append(text)
        try:
            home = tempfile.mkdtemp(prefix="bb-fatal-")
            os.environ["BRAVELY_BUILDER_HOME"] = home
            app.start_failed(ImportError("cannot import name 'crash' from 'core'"))
        finally:
            app.fatal_box = old
        self.assertTrue(shown and "could not start" in shown[0] and "download the package again" in shown[0])
        self.assertIn("cannot import name", open(os.path.join(home, "crash.log"), encoding="utf-8").read())


if __name__ == "__main__":
    unittest.main()
