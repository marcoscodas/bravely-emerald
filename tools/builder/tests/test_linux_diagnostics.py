"""Run the real Linux build script up to a failed first enter using local stand-ins."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from core import Builder                  # noqa: E402
from core.report import full_report       # noqa: E402

BUILD_SCRIPT = Path(__file__).resolve().parents[3] / "tools/deck/build_deck.sh"
CONTAINER = "integration-build"
CONTAINER_ID = "a1b2c3d4e5f6"
PRIVATE_TRACE = "PRIVATE_DISTROBOX_TRACE"
PRIVATE_LOG = "PRIVATE_PODMAN_LOG"
SYSTEM_TOOLS = ("awk", "bash", "dirname", "grep", "mkdir")

STUB = r'''
import json
import os
from datetime import datetime, timedelta, timezone
from pathlib import Path
import sys

tool = Path(sys.argv[1]).name
args = sys.argv[2:]
work = Path(os.environ["BRAVELY_TEST_WORK"])
with (work / "calls").open("a", encoding="utf-8") as calls:
    calls.write(json.dumps([tool] + args) + "\n")

name = "integration-build"
container_id = "a1b2c3d4e5f6"
if tool == "distrobox":
    if args == ["--version"]:
        print("distrobox: 1.7.0")
    elif args == ["list"]:
        print("ID | NAME | STATUS | IMAGE")
        print(f"{container_id} | {name} | Exited | archlinux")
    elif args == ["enter", "--name", name, "--no-tty", "--", "bash", "-lc", "echo container-ok"]:
        (work / "entered").write_text(datetime.now(timezone.utc).isoformat(), encoding="utf-8")
        print("Installing basic packages... Error: An error occurred", file=sys.stderr)
        print("+ printf PRIVATE_DISTROBOX_TRACE", file=sys.stderr)
        sys.exit(1)
    else:
        sys.exit("Unexpected Distrobox command")
elif args == ["--version"]:
    print("podman version 4.9.3")
elif args[0] == "logs" and args[-1] == container_id:
    entered = datetime.fromisoformat((work / "entered").read_text(encoding="utf-8"))
    conflict = ":: installing openssl (4.1) breaks dependency 'libcrypto.so=3-64'"
    # Deliberately include old, future and unstructured lines even when the caller requests a window.
    print(f"{(entered - timedelta(minutes=1)).isoformat()} {conflict}")
    print(f"{(entered + timedelta(minutes=1)).isoformat()} {conflict}")
    print("chpasswd: invalid password hash")
    print(f"{entered.isoformat()} + printf PRIVATE_PODMAN_LOG", file=sys.stderr)
    if os.environ["BRAVELY_TEST_SPECIFIC_LOG"] == "yes":
        print(f"{entered.isoformat()} chpasswd: (line 1, user private-user)", file=sys.stderr)
        print(f"{entered.isoformat()} invalid password hash", file=sys.stderr)
    else:
        print(f"{entered.isoformat()} Error: An error occurred")
else:
    sys.exit("Unexpected Podman command")
'''


@unittest.skipUnless(sys.platform.startswith("linux") and all(shutil.which(tool) for tool in SYSTEM_TOOLS),
                     "needs Bash and basic Linux utilities")
class BuildDeckDiagnosticsTests(unittest.TestCase):
    def setUp(self):
        work = tempfile.TemporaryDirectory(prefix="bb-deck-diagnostics-", dir="/tmp")
        self.addCleanup(work.cleanup)
        self.root = Path(work.name)
        self.script = self.root / "tools/deck/build_deck.sh"
        self.script.parent.mkdir(parents=True)
        shutil.copyfile(BUILD_SCRIPT, self.script)
        # Empty files satisfy the existing assets check so the script needs no ROM.
        for rel in (".assets_ok", "assets/ui2/ui_atlas.bin", "graphics/pokemon/bulbasaur/front.png"):
            asset = self.root / rel
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.touch()

        self.bin = self.root / "bin"
        self.bin.mkdir()
        # Limit PATH so a fixture mistake cannot reach the real runtime, curl or a package manager.
        for name in SYSTEM_TOOLS:
            (self.bin / name).symlink_to(shutil.which(name))
        stub = self.root / "container_stub.py"
        stub.write_text(STUB, encoding="utf-8")
        for name in ("distrobox", "podman"):
            self.tool(name, "exec %s -B %s \"$0\" \"$@\"" % (shlex.quote(sys.executable), shlex.quote(str(stub))))
        self.tool("id", "printf '1000\\n'")
        self.tool("uname", "printf 'x86_64\\n'")
        self.tool("df", "printf '%s\\n' 'header' '/dev/test 20000 1 19999 1% /'")
        self.tool("curl", "exit 0")

    def tool(self, name, script):
        path = self.bin / name
        path.write_text("#!/bin/sh\n" + script + "\n", encoding="utf-8")
        path.chmod(0o700)

    def environment(self, specific_log):
        return dict(os.environ, PATH=str(self.bin), BASH_ENV="", BRAVELY_FAKE_HOST="linux",
                    BRAVELY_BUILDER_HOME=str(self.root / "settings"), BRAVELY_TEST_WORK=str(self.root),
                    BRAVELY_TEST_SPECIFIC_LOG="yes" if specific_log else "no")

    def calls(self):
        return [json.loads(line) for line in (self.root / "calls").read_text(encoding="utf-8").splitlines()]

    def failed_build(self, specific_log):
        with mock.patch.dict(os.environ, self.environment(specific_log)), \
                mock.patch.object(Builder, "have_net", return_value=True), \
                mock.patch("core.builder.free_gb", return_value=50):
            b = Builder(str(self.root))
            plan = b.plan_build({"targets": ["linux32"], "dir": str(self.root / "out"),
                                 "jobs": 1, "container": CONTAINER})
            task = b.start(*plan)
            with task.cond:
                self.assertTrue(task.cond.wait_for(lambda: task.state != "running", timeout=12))

        self.assertEqual(task.state, "failed")
        self.assertEqual(task.final["code"], 1)
        log = Path(task.logfile).read_text(encoding="utf-8")
        for output in (log, full_report(task)):
            self.assertIn("Probable cause: " + task.hints[0]["title"], output)
            self.assertNotIn(PRIVATE_TRACE, output)
            self.assertNotIn(PRIVATE_LOG, output)
            self.assertNotIn("private-user", output)
        calls = self.calls()
        self.assertNotIn("create", [argv[1] for argv in calls])
        self.assertNotIn("rm", [argv[1] for argv in calls])
        log_commands = [argv for argv in calls if argv[:2] == ["podman", "logs"]]
        self.assertEqual(len(log_commands), 1)
        self.assertEqual(log_commands[0][-4:], ["--tail", "100", "--", CONTAINER_ID])
        return task.hints[0]

    def test_failed_enter_uses_only_log_records_from_its_window(self):
        hint = self.failed_build(specific_log=True)
        self.assertIn("chpasswd: invalid password hash", hint["title"])

    def test_no_useful_podman_records_leave_local_recovery_instructions(self):
        hint = self.failed_build(specific_log=False)
        self.assertIn("unknown reason", hint["title"])
        self.assertIn("podman logs --tail 100 " + CONTAINER, hint["fix"])

    def test_direct_script_leaves_distrobox_stderr_in_the_terminal(self):
        run = subprocess.run(["bash", str(self.script), "--dir", str(self.root / "out"),
                              "--jobs", "1", "--container", CONTAINER, "--no-run"],
                             env=self.environment(specific_log=False), capture_output=True, text=True, timeout=12)
        self.assertEqual(run.returncode, 1)
        self.assertIn(PRIVATE_TRACE, run.stderr)
        self.assertIn("the container does not start", run.stderr)
        self.assertTrue(all(argv[0] == "distrobox" for argv in self.calls()))
