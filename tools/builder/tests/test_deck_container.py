"""Exercise the Deck build entry point with simulated host and container commands."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from tools.builder.core.errors import explain


SCRIPT = Path(__file__).resolve().parents[2] / "deck" / "build_deck.sh"
BASH = shutil.which("bash")

DISTROBOX = r"""
if [[ $1 == --version ]]; then echo "distrobox: $STUB_VERSION"; exit 0; fi
printf '%s\n' "$*" >> "$STUB_DIR/calls"
container="$STUB_DIR/container"
case "$1" in
    list)
        if [[ $STUB_FAILURE == list ]]; then
            echo "Podman unavailable" >&2
            exit 1
        fi
        echo 'ID | NAME | STATUS | IMAGE'
        if [[ -f "$container" && $STUB_FAILURE != unlisted_partial ]]; then
            echo 'abc | emerald-build | Exited | archlinux:latest'
        fi
        ;;
    create)
        if [[ $2 == --help ]]; then
            echo "distrobox version: $STUB_VERSION"
            if [[ $STUB_VERSION != legacy ]]; then
                printf '%s\n' '  --pre-init-hooks: run commands before initialization'
            fi
            exit 0
        fi
        if [[ -f "$container" ]]; then
            echo "Distrobox named 'emerald-build' already exists."
            exit 0
        fi
        case "$STUB_FAILURE" in
            create) exit 1 ;;
            partial|unlisted_partial)
                : > "$container"
                echo 'simulated failure after container creation' >&2
                exit 1
                ;;
            transient)
                if [[ ! -f "$STUB_DIR/retried" ]]; then
                    : > "$STUB_DIR/retried"
                    exit 1
                fi
                ;;
        esac
        : > "$container"
        if [[ ${7-} == --pre-init-hooks ]]; then
            [[ $# == 8 ]] || exit 90
            printf '%s\n' "$8" > "$STUB_DIR/hook"
        fi
        ;;
    enter)
        if [[ $STUB_FAILURE == disappeared ]]; then
            rm -f "$container"
        fi
        if [[ ! -f "$container" ]]; then
            if [[ $DBX_NON_INTERACTIVE == 1 ]]; then
                response=yes
            else
                printf 'Create it now, out of the default image? [Y/n]: ' >&2
                read -r response || exit 1
                response=${response:-Y}
            fi
            case "$response" in
                y|Y|Yes|yes|YES)
                    echo 'alternative container created' >> "$STUB_DIR/calls"
                    : > "$container"
                    rm -f "$STUB_DIR/hook"
                    ;;
                n|N|No|no|NO)
                    echo 'alternative creation declined' >> "$STUB_DIR/calls"
                    exit 0
                    ;;
                *) exit 1 ;;
            esac
        fi
        case "${!#}" in
            'echo container-ok')
                if [[ $STUB_EXISTING == 0 ]]; then
                    if [[ -f "$STUB_DIR/hook" ]]; then
                        # Like distrobox-init, evaluate the hook before package setup.
                        # Leave errexit off here to verify the hook explicitly aborts.
                        sh -c 'eval $1; echo "basic packages" >> "$STUB_DIR/calls"' \
                            init "$(< "$STUB_DIR/hook")" || exit 1
                    else
                        echo 'basic packages' >> "$STUB_DIR/calls"
                    fi
                    if [[ $STUB_FAILURE == chpasswd ]]; then
                        echo 'chpasswd: invalid password hash' >&2
                        exit 1
                    fi
                fi
                echo container-ok
                ;;
            'pacman -Q '*) [[ $STUB_EXISTING == 1 ]] || exit 1 ;;
            './run.sh') echo 'game launched' >> "$STUB_DIR/calls" ;;
            '[ -d '*|'sudo bash -c '*|'gcc -m32 '*|'cmake -S '*|'cmake --build '*) ;;
            *) exit 90 ;;
        esac
        ;;
    *) exit 90 ;;
esac
"""


@unittest.skipUnless(sys.platform.startswith("linux") and BASH, "requires Linux and Bash")
class DeckContainerTests(unittest.TestCase):
    def run_builder(self, os_release="ID=linuxmint", version="1.8.2.5", existing=False, failure="",
                    launch=None):
        with tempfile.TemporaryDirectory(prefix="deck-container-") as directory:
            root = Path(directory)
            bin_dir = root / "bin"
            bin_dir.mkdir()
            script = root / "tools/deck/build_deck.sh"
            script.parent.mkdir(parents=True)
            shutil.copyfile(SCRIPT, script)
            (root / "os-release").write_text(os_release + "\n")
            if existing:
                (root / "container").touch()
            for name in ("awk", "cat", "chmod", "dirname", "ln", "mkdir", "rm", "sh"):
                (bin_dir / name).symlink_to(shutil.which(name))
            stubs = {
                "distrobox": DISTROBOX,
                "curl": "exit 0",
                "podman": 'printf "podman %s\n" "$*" >> "$STUB_DIR/calls"\nexit 90',
                "id": "echo 1000",
                "uname": "echo x86_64",
                "nproc": "echo 2",
                "sleep": "exit 0",
                "df": "printf 'header\nfs 0 0 20000\n'",
                "grep": 'args=("$@")\ncase "${!#}" in */os-release) args[-1]="$STUB_DIR/os-release";; esac\n'
                        f'exec "{shutil.which("grep")}" "${{args[@]}}"',
                "pacman": 'printf "pacman %s\n" "$*" >> "$STUB_DIR/calls"\n'
                          '[[ "$*" == "-Syu --noconfirm" ]] || exit 90\n'
                          'if [[ $STUB_FAILURE == upgrade ]]; then echo "simulated pacman failure" >&2; exit 42; fi',
            }
            for name, body in stubs.items():
                path = bin_dir / name
                path.write_text(f"#!{BASH}\n{body}\n")
                path.chmod(0o755)
            for rel in (
                ".assets_ok", "assets/ui2/ui_atlas.bin", "assets/ui/intro_margins.bin",
                "out/deps32/lib/libSDL2.a", "out/deps32/lib/libSDL2_image.a",
                "out/build/dist/run.sh", "out/userdata/settings.txt", "out/build/dist/pokeemerald",
            ):
                path = root / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            (root / "out/build/dist/pokeemerald").chmod(0o755)
            (root / "out/build/dist/app0/voxel").mkdir(parents=True)

            env = {"HOME": directory, "PATH": str(bin_dir), "LC_ALL": "C", "STUB_DIR": directory,
                   "STUB_VERSION": version, "STUB_EXISTING": str(int(existing)), "STUB_FAILURE": failure,
                   "DBX_NON_INTERACTIVE": "1"}
            result = subprocess.run(
                [BASH, str(script), "--dir", str(root / "out"), "--no-run"],
                cwd=root, text=True, capture_output=True, timeout=10, env=env,
            )
            if launch is not None:
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                (root / "calls").write_text("")
                if launch == "missing":
                    (root / "container").unlink()
                result = subprocess.run(
                    [str(root / "out/emerald-launch.sh")],
                    cwd=root, text=True, capture_output=True, timeout=10,
                    env=dict(env, XAUTHORITY=str(root / "xauth"),
                             STUB_FAILURE="disappeared" if launch == "race" else ""),
                )
            calls = (root / "calls").read_text().splitlines()
            if failure in ("partial", "unlisted_partial"):
                self.assertTrue((root / "container").is_file(), "partial container was removed")
            return result, calls

    def test_desktop_upgrades_before_basic_packages_and_multilib(self):
        result, calls = self.run_builder()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(calls[:6], [
            "list",
            "create --help",
            "create --yes --name emerald-build --image docker.io/library/archlinux:latest "
            "--pre-init-hooks pacman -Syu --noconfirm || exit",
            "enter --name emerald-build --no-tty -- bash -lc echo container-ok",
            "pacman -Syu --noconfirm",
            "basic packages",
        ])
        self.assertTrue(any("pacman -Syu --noconfirm && pacman -S --needed" in call for call in calls[6:]))

    def test_existing_container_needs_no_hook_support(self):
        result, calls = self.run_builder(existing=True, version="legacy")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("container exists", result.stdout)
        self.assertEqual(calls[0], "list")
        self.assertTrue(all(call.startswith("enter ") for call in calls[1:]), calls)
        self.assertFalse(any("pacman -S" in call for call in calls), calls)

    def test_steamos_creates_without_hook_or_capability_check(self):
        for os_release in ('ID="steamos"', "ID=arch\nVARIANT_ID='steamdeck'"):
            with self.subTest(os_release=os_release):
                result, calls = self.run_builder(os_release, version="legacy")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(calls[1], "create --yes --name emerald-build --image docker.io/library/archlinux:latest")
                self.assertEqual(calls[3], "basic packages")
                self.assertTrue(any("pacman -Syu --noconfirm && pacman -S --needed" in call for call in calls[4:]))

    def test_pre_init_failure_stops_before_basic_packages(self):
        for version in ("1.8.2.5", "1.8.3"):
            with self.subTest(version=version):
                result, calls = self.run_builder(version=version, failure="upgrade")
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("Distrobox initialization failed for 'emerald-build'", result.stderr)
                self.assertIn("Inspect its local log", result.stderr)
                self.assertIn("podman logs --tail 100 emerald-build", result.stderr)
                output = result.stdout + result.stderr
                self.assertNotIn("internal disk", output)
                self.assertNotIn("simulated pacman failure", output)
                self.assertEqual(explain(result.stderr.splitlines()), [])
                self.assertEqual(len(calls), 5, calls)
                self.assertEqual(calls[-1], "pacman -Syu --noconfirm")

    def test_chpasswd_failure_still_reports_the_local_log(self):
        result, calls = self.run_builder(version="1.8.2.5", failure="chpasswd")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("podman logs --tail 100 emerald-build", result.stderr)
        self.assertNotIn("chpasswd: invalid password hash", result.stdout + result.stderr)
        self.assertEqual(calls[4:], ["pacman -Syu --noconfirm", "basic packages"])

    def test_old_distrobox_stops_before_creating_a_container(self):
        for version in ("1.7.0", "1.8.2.4"):
            with self.subTest(version=version):
                result, calls = self.run_builder(version=version)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("Distrobox %s is too old" % version, result.stderr)
                self.assertIn("Install Distrobox 1.8.2.5 or newer", result.stderr)
                self.assertEqual(calls, ["list"])

    def test_old_distrobox_does_not_block_steamos_or_existing_containers(self):
        for kwargs in ({"os_release": "ID=steamos"}, {"existing": True}):
            with self.subTest(**kwargs):
                result, calls = self.run_builder(version="1.7.0", **kwargs)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_new_desktop_container_requires_hook_support(self):
        result, calls = self.run_builder(version="legacy")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("requires Distrobox with --pre-init-hooks", result.stderr)
        self.assertEqual(calls, ["list", "create --help"])

    def test_list_failure_never_creates_or_removes_a_container(self):
        result, calls = self.run_builder(existing=True, failure="list")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Podman unavailable", result.stderr)
        self.assertEqual(calls, ["list"])

    def test_create_retries_without_removal_on_steamos(self):
        result, calls = self.run_builder("ID=steamos", failure="create")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("distrobox create failed 3 times", result.stderr)
        create = "create --yes --name emerald-build --image docker.io/library/archlinux:latest"
        self.assertEqual(calls, ["list", create, "list", create, "list", create, "list"])

    def test_steamos_retries_successfully_if_the_name_is_still_free(self):
        result, calls = self.run_builder("ID=steamos", failure="transient")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        create = "create --yes --name emerald-build --image docker.io/library/archlinux:latest"
        self.assertEqual(calls[:4], ["list", create, "list", create])
        self.assertEqual(calls[5], "basic packages")

    def test_disappeared_container_declines_creation_and_fails_without_probe_output(self):
        for version in ("1.8.2.5", "1.8.3"):
            with self.subTest(version=version):
                result, calls = self.run_builder(version=version, failure="disappeared")
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("Distrobox did not run the startup check", result.stderr)
                self.assertEqual(calls[3:], [
                    "enter --name emerald-build --no-tty -- bash -lc echo container-ok",
                    "alternative creation declined",
                ])

    def test_failed_create_stops_when_it_leaves_a_container(self):
        for os_release in ("ID=linuxmint", "ID=steamos"):
            with self.subTest(os_release=os_release):
                result, calls = self.run_builder(os_release, failure="partial")
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("exists after a failed creation attempt", result.stderr)
                self.assertEqual(sum(call.startswith("create --yes") for call in calls), 1, calls)
                self.assertEqual(calls[-1], "list")
                self.assertFalse(any(call.startswith(("enter ", "rm ")) for call in calls), calls)

    def test_generated_launcher_fails_when_container_is_missing(self):
        result, calls = self.run_builder(existing=True, launch="missing")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Build container 'emerald-build' is missing", result.stderr)
        self.assertEqual(calls, ["list"])

    def test_generated_launcher_declines_creation_if_container_disappears_after_check(self):
        _, calls = self.run_builder(existing=True, launch="race")
        self.assertEqual(calls[0], "list")
        self.assertTrue(calls[-2].endswith("./run.sh"), calls)
        self.assertEqual(calls[-1], "alternative creation declined")
        self.assertNotIn("alternative container created", calls)

    def test_generated_launcher_runs_game_with_existing_container(self):
        result, calls = self.run_builder(os_release="ID=steamos", existing=True, launch="present")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(calls[0], "list")
        self.assertTrue(calls[1].endswith("./run.sh"), calls)
        self.assertEqual(calls[2:], ["game launched"])

    def test_create_status_zero_for_unlisted_container_does_not_count_as_success(self):
        result, calls = self.run_builder(failure="unlisted_partial")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("already exists", result.stdout)
        self.assertIn("the name is already in use", result.stderr)
        self.assertEqual(sum(call.startswith("create --yes") for call in calls), 2, calls)
        self.assertFalse(any(call.startswith(("enter ", "rm ")) for call in calls), calls)
