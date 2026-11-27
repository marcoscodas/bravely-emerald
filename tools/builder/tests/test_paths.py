"""Folders the build scripts cannot work in are refused with a plain message before anything starts; short user names and
plain paths such as the one in issue 3 are accepted."""
import hashlib
import os
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-paths-"))

from core.validate import source_path_problem     # noqa: E402
from core import validate                         # noqa: E402
from core import Builder                          # noqa: E402
from cli import write_synthetic_rom               # noqa: E402
from core import source                           # noqa: E402


class PathProblemTests(unittest.TestCase):
    def test_accepted(self):
        for p in ("C:\\Users\\Player\\AppData\\Local\\BravelyEmerald\\source", "F:\\fresh-test\\home\\source", "C:\\BravelyEmerald\\source",
                  "/home/deck/emerald-deck"):
            self.assertIsNone(source_path_problem(p), p)

    def test_refused_with_a_plain_message(self):
        cases = {"C:\\Users\\Ana Maria\\AppData\\Local\\x": "space", "C:\\Users\\Jos\u00e9\\AppData\\Local\\x": "accent",
                 "\\\\server\\share\\x": "network", "C:\\a'b": "symbols", "C:\\" + "x" * 120: "long"}
        for p, word in cases.items():
            msg = source_path_problem(p)
            self.assertTrue(msg and "BravelyEmerald" in msg or word == "symbols" or word == "long", (p, msg))
            self.assertIsNotNone(msg, p)

    def test_default_folder_falls_back_for_accents_and_spaces(self):
        with mock.patch.object(source.os, "name", "nt"), \
             mock.patch.dict(os.environ, {"LOCALAPPDATA": "C:\\Users\\Jos\u00e9\\AppData\\Local", "SystemDrive": "C:"}, clear=False):
            os.environ.pop("BRAVELY_BUILDER_HOME", None)
            try:
                p = source.default_work_dir()
            finally:
                os.environ["BRAVELY_BUILDER_HOME"] = tempfile.mkdtemp(prefix="bb-paths-")
        self.assertTrue(p.replace("/", "\\").lower().endswith("c:\\bravelyemerald\\source"), p)


class SpacedBuildFolderTests(unittest.TestCase):
    """Linux install folder with a space (RecompLauncher's "Recomp Library"): accepted, and the build parts go in a folder
    without spaces, because a build inside a spaced folder fails (pkgconf splits SDL2's path)."""

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="bb-space-")
        self.spaced = os.path.join(self.root, "Recomp Library", "bravely-emerald", "game")
        self.home = os.path.join(self.root, "home-default")

    def name(self, d):
        return "bravely-emerald-parts-" + hashlib.sha1(d.encode()).hexdigest()[:8]

    def test_the_note_names_both_folders(self):
        text = validate.parts_note(self.spaced, "/x/parts")
        for part in ("space", self.spaced, "/x/parts"):
            self.assertIn(part, text)

    def test_picks_a_folder_above_the_space_on_the_same_drive(self):
        want = os.path.join(self.root, self.name(self.spaced))
        self.assertEqual(validate.suggest_parts_dir(self.spaced, self.home), want)

    def test_two_install_folders_never_share_the_build(self):
        other = os.path.join(self.root, "Recomp Library", "second", "game")
        self.assertNotEqual(validate.suggest_parts_dir(self.spaced, self.home),
                            validate.suggest_parts_dir(other, self.home))

    def test_falls_back_when_the_space_is_in_the_drive_name(self):
        card = os.path.join(self.root, "My Card")
        os.makedirs(card)
        d = os.path.join(card, "game")
        with mock.patch.object(validate, "_drive", lambda p: 2 if p.startswith(card) else 1):
            got = validate.suggest_parts_dir(d, self.home)
        self.assertEqual(got, self.home + self.name(d)[len("bravely-emerald"):])

    def test_falls_back_when_the_folder_above_is_not_writable(self):
        with mock.patch.object(validate.os, "access", lambda p, mode: False):
            self.assertTrue(validate.suggest_parts_dir(self.spaced, self.home).startswith(self.home + "-parts-"))

    def plan(self, **extra):
        rom = os.path.join(self.root, "Pokemon - Emerald Version (USA, Europe).gba")
        write_synthetic_rom(rom)
        b = Builder(self.root, True)
        b.probe = mock.Mock(assets=mock.Mock(return_value=False))
        opts = dict({"targets": ["linux32"], "rom": rom, "jobs": 2, "dir": self.spaced}, **extra)
        with mock.patch.object(b, "targets", lambda: [{"id": "linux32", "available": True, "why": ""}]), \
             mock.patch("core.builder.source_path_problem", lambda *a: None):
            _, _, steps, meta = b.plan_build(opts)
        return rom, steps[0].argv, meta

    def test_plan_moves_the_parts_for_a_spaced_folder_by_itself(self):
        rom, argv, meta = self.plan()
        spaced = os.path.normpath(self.spaced)
        want = os.path.join(self.root, self.name(spaced))
        self.assertEqual(argv[argv.index("--dir") + 1], spaced)
        self.assertEqual(argv[argv.index("--rom") + 1], os.path.normpath(rom))
        self.assertEqual(argv[argv.index("--parts-dir") + 1], want)
        self.assertEqual(meta["parts"], want)

    def test_plan_keeps_everything_together_without_a_space(self):
        plain = os.path.join(self.root, "plain")
        _, argv, meta = self.plan(dir=plain)
        self.assertNotIn("--parts-dir", argv)
        self.assertEqual(meta["parts"], plain)

    def test_plan_passes_a_parts_folder(self):
        parts = os.path.join(self.root, "parts")
        _, argv, meta = self.plan(parts_dir=parts)
        self.assertEqual(argv[argv.index("--parts-dir") + 1], os.path.normpath(parts))
        self.assertEqual(meta["parts"], os.path.normpath(parts))

    def test_plan_refuses_a_parts_folder_with_a_space(self):
        with self.assertRaises(ValueError):
            self.plan(parts_dir=os.path.join(self.root, "more parts"))


if __name__ == "__main__":
    unittest.main()
