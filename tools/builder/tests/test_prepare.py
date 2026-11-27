"""--prepare and --game (RecompLauncher setup step): fast tests, no build is run."""
import contextlib
import io
import os
import sys
import tempfile
import unittest
import zipfile

BUILDER = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BUILDER)

import cli  # noqa: E402


class FakeBuilder:
    host = {"kind": "windows"}

    def __init__(self, mb):
        self.mb = mb

    def plan(self, target):
        return [{"id": "x", "mb": self.mb}]


def run_prepare(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = cli.main(["prepare"] + argv)
    return rc, out.getvalue()


class PrepareTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="bb-prep-")
        self.rom = os.path.join(self.tmp, "emerald.gba")
        cli.write_synthetic_rom(self.rom)
        self._setup = cli.setup_stdout
        cli.setup_stdout = lambda: None              # keep the redirected stdout of the test

    def tearDown(self):
        cli.setup_stdout = self._setup

    def test_progress_line_is_clamped_and_formatted(self):
        self.assertEqual(cli.progress_line(0.4, "Compiling"), "progress 0.40 Compiling")
        self.assertEqual(cli.progress_line(-1, "a"), "progress 0.00 a")
        self.assertEqual(cli.progress_line(7, "a"), "progress 1.00 a")

    def test_stage_fraction_follows_the_stage_list(self):
        stages = ["prereq", "files", "compile", "package"]
        self.assertEqual(cli.stage_fraction(stages, "prereq"), 0.0)
        self.assertEqual(cli.stage_fraction(stages, "compile"), 0.5)
        self.assertLess(cli.stage_fraction(stages, "package"), 1.0)
        self.assertEqual(cli.stage_fraction(stages, "unknown"), 0.0)

    def test_every_stage_of_the_planner_has_a_text(self):
        for stage in ("prereq", "install", "container", "files", "tools", "assets", "deps", "compile", "link", "package", "test"):
            self.assertIn(stage, cli.STAGE_TEXT)

    def test_rom_from_zip(self):
        zp = os.path.join(self.tmp, "emerald.zip")
        with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(self.rom, "Pokemon - Emerald.gba")
            z.writestr("readme.txt", "x")
        dest = tempfile.mkdtemp(prefix="bb-zip-")
        path = cli.rom_from_zip(zp, dest)
        self.assertEqual(os.path.getsize(path), 16 * 1024 * 1024)
        two = os.path.join(self.tmp, "two.zip")
        with zipfile.ZipFile(two, "w") as z:
            z.writestr("a.gba", "1")
            z.writestr("b.gba", "2")
        with self.assertRaises(ValueError):
            cli.rom_from_zip(two, dest)

    def test_missing_game_is_exit_2_with_a_plain_message(self):
        rc, out = run_prepare(["--out", self.tmp])
        self.assertEqual(rc, 2)
        self.assertIn("ERROR: No game file given", out)

    def test_not_a_rom_is_exit_2(self):
        bad = os.path.join(self.tmp, "bad.gba")
        with open(bad, "wb") as f:
            f.write(b"not a rom")
        rc, out = run_prepare(["--game", bad, "--out", self.tmp])
        self.assertEqual(rc, 2)
        self.assertIn("Not an Emerald dump", out)

    def test_downloads_need_the_flag(self):
        old = cli.make_builder
        cli.make_builder = lambda repo, dry=False, force=False: (FakeBuilder(900), None)
        try:
            rc, out = run_prepare(["--game", self.rom, "--out", os.path.join(self.tmp, "game")])
        finally:
            cli.make_builder = old
        self.assertEqual(rc, 4)
        self.assertIn("--allow-downloads", out)
        self.assertIn("progress 0.00", out)               # the ROM check is reported first

    def test_build_accepts_game_as_the_rom_option(self):
        seen = {}
        old_make = cli.make_builder
        old_run = cli.run_task

        class B(FakeBuilder):
            def plan_build(self, opts):
                seen.update(opts)
                raise ValueError("stop here")

            def plan_text(self, target):
                return ""
        cli.make_builder = lambda repo, dry=False, force=False: (B(0), None)
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                rc = cli.main(["build", "--target", "windows", "--game", self.rom])
        finally:
            cli.make_builder = old_make
            cli.run_task = old_run
        self.assertEqual(rc, 2)
        self.assertEqual(seen["rom"], self.rom)

    def prepare_output(self, parts):
        """prepare with a planner that returns these build parts; the build itself fails at once."""
        out_dir = os.path.join(self.tmp, "Recomp Library")

        class Task:
            final, cur_step, logfile, hints = {"step": "x"}, "x", "log.txt", []

        class B(FakeBuilder):
            def plan_build(self, opts):
                return "Linux", ["prereq"], [], {"dir": out_dir, "parts": parts or out_dir}

            def start(self, label, stages, steps, meta, kind, listener):
                listener({"type": "state", "state": "failed"})
                return Task()
        old = cli.make_builder
        cli.make_builder = lambda repo, dry=False, force=False: (B(0), None)
        try:
            return run_prepare(["--game", self.rom, "--target", "linux32", "--out", out_dir])[1]
        finally:
            cli.make_builder = old

    def test_prepare_says_where_the_build_files_go_when_they_are_elsewhere(self):
        self.assertIn("/x/parts", self.prepare_output("/x/parts"))
        self.assertNotIn("space", self.prepare_output(""))

    def test_app_routes_prepare_to_the_cli(self):
        import app
        import cli as cli_mod
        got = []
        old = cli_mod.main
        cli_mod.main = lambda argv: got.append(argv) or 0
        try:
            self.assertEqual(app.main(["--prepare", "--game", "X", "--out", "Y"]), 0)
        finally:
            cli_mod.main = old
        self.assertEqual(got, [["prepare", "--game", "X", "--out", "Y"]])


if __name__ == "__main__":
    unittest.main()
