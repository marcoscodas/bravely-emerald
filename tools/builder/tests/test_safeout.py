"""SafeOut: a stream that opens but cannot be written to must not crash the CLI (OSError 22 from a windowed exe)."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import cli  # noqa: E402


class Broken:
    def write(self, text):
        raise OSError(22, "Invalid argument")

    def flush(self):
        raise OSError(22, "Invalid argument")


class SafeOutTests(unittest.TestCase):
    def test_broken_stream_goes_to_log(self):
        home = tempfile.mkdtemp(prefix="bb-safeout-")
        import core.settings as settings
        old = settings.app_home
        settings.app_home = lambda: home
        try:
            out = cli.SafeOut(Broken())
            print("  extracted 500 files", file=out, flush=True)
            out.flush()
            out.fallback.close()
            with open(os.path.join(home, "cli.log"), encoding="utf-8") as f:
                self.assertIn("extracted 500 files", f.read())
        finally:
            settings.app_home = old

    def test_good_stream_unchanged(self):
        import io
        buf = io.StringIO()
        out = cli.SafeOut(buf)
        print("hello", file=out, flush=True)
        self.assertEqual(buf.getvalue(), "hello\n")
        self.assertIsNone(out.fallback)


if __name__ == "__main__":
    unittest.main()
