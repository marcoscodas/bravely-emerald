#!/usr/bin/env python3
"""Bravely Emerald builder: desktop app (PySide6). Builds the game from your own ROM.

    pip install PySide6          (PySide6-Essentials is enough)
    python tools/builder/app.py [--repo DIR] [--dry-run] [--smoke]

--selftest extracts the bundled source and dry-runs this host's build; --build --rom X --out DIR builds without a window.
--prepare --game X --out DIR [--allow-downloads] is the same build for RecompLauncher: it prints "progress 0.4 Text" lines.
--smoke opens the window, lets it run for a moment and quits (used to test the packaged app).
The logic is in tools/builder/core (no Qt); the windows are in tools/builder/ui. See docs/BUILDER.md.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)


def check_imports():
    """--imports: import every module of core and ui and print OK or FAIL for each. The list comes from the bundle (modules.txt,
    written by package.py from the files on disk), so a module missing from the frozen app is found."""
    import importlib
    names = None
    try:
        from core.source import bundle_dir
        bd = bundle_dir()
        if bd and os.path.isfile(os.path.join(bd, "modules.txt")):
            with open(os.path.join(bd, "modules.txt"), encoding="utf-8") as f:
                names = [ln.strip() for ln in f if ln.strip()]
    except Exception as e:                           # core itself is broken: say so
        print("FAIL core (%r)" % (e,))
        return 1
    if names is None:                                # a checkout: the files on disk
        names = []
        for pkg in ("core", "ui"):
            for fn in sorted(os.listdir(os.path.join(HERE, pkg))):
                if fn.endswith(".py"):
                    names.append(pkg if fn == "__init__.py" else "%s.%s" % (pkg, fn[:-3]))
    bad = 0
    for n in names:
        try:
            importlib.import_module(n)
            print("OK   %s" % n)
        except BaseException as e:
            bad += 1
            print("FAIL %s: %r" % (n, e))
    print("IMPORTS %s: %d modules, %d failed" % ("OK" if not bad else "FAILED", len(names), bad))
    return 1 if bad else 0


def fatal_box(title, text):
    """A plain native message box (no Qt, no Tk) for errors before the window exists; stderr elsewhere."""
    try:
        if os.name == "nt":
            import ctypes
            ctypes.windll.user32.MessageBoxW(0, text, title, 0x10)
            return
    except Exception:
        pass
    try:
        sys.stderr.write("%s: %s\n" % (title, text))
    except Exception:
        pass


ISSUES_URL = "https://github.com/marcoscodas/bravely-emerald/issues"


def ui_problem_message(exc, frozen):
    """Message for a failure to load the window library, with a code that tells us which case a player hit; None for
    other failures."""
    names = "%s %r" % (getattr(exc, "name", "") or "", exc)
    if not isinstance(exc, ImportError) or not any(w in names for w in ("PySide6", "shiboken", "Qt")):
        return None
    what = repr(exc)[:300]
    if frozen:
        return ("[BB-UI-DOWNLOAD] The builder could not load its window files.\n\n%s\n\n"
                "The download may be incomplete, or something (such as an antivirus) removed files from it. Download the "
                "builder again and unpack it into an empty folder. Installing PySide6 yourself will not help: the downloaded "
                "builder only uses its own copy. On Linux, if the line above names a missing .so file, install that system "
                "library.\n\nIf it still happens, send us this message (code BB-UI-DOWNLOAD):\n%s" % (what, ISSUES_URL))
    return ("[BB-UI-SOURCE] You are running the builder from source, and Python cannot load PySide6.\n\n%s\n\n"
            "Install it:  python -m pip install PySide6-Essentials\nhttps://pypi.org/project/PySide6/\n\n"
            "Or use the downloaded builder, which includes it:\n"
            "https://github.com/marcoscodas/bravely-emerald/releases/latest" % what)


def start_failed(exc):
    """The app could not start (a missing module, a broken install): log it and tell the user in plain words."""
    import traceback
    tb = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
    where = os.path.dirname(sys.executable) if getattr(sys, "frozen", False) else HERE
    for base in (os.environ.get("BRAVELY_BUILDER_HOME"), where, os.environ.get("TEMP") or "/tmp"):
        if not base:
            continue
        try:
            path = os.path.join(base, "crash.log")
            with open(path, "a", encoding="utf-8") as f:
                f.write(tb + "\n")
            break
        except OSError:
            path = "(the log could not be saved)"
    ui_text = ui_problem_message(exc, bool(getattr(sys, "frozen", False)))
    if ui_text:
        fatal_box("Bravely Emerald Builder", "%s\n\nDetails were saved in:\n%s" % (ui_text, path))
        return
    fatal_box("Bravely Emerald Builder", "The builder could not start.\n\n%s\n\nThe app files are probably incomplete: download the "
              "package again and unpack it into an empty folder.\n\nDetails were saved in:\n%s" % (repr(exc)[:300], path))


def main(argv=None):
    # headless modes, used to test the packaged exe: they run the core without a window (see cli.py)
    if argv is None:
        argv = sys.argv[1:]
    if "--probe-screen" in argv:                      # used by core/scale.py to learn the screen before Qt is configured
        from PySide6.QtGui import QGuiApplication
        import json
        qa = QGuiApplication([])
        sc = qa.primaryScreen()
        g, d = sc.geometry(), sc.devicePixelRatio()
        line = json.dumps([int(g.width() * d), int(g.height() * d), d])
        if sys.stdout is not None:
            print(line, flush=True)
        else:
            os.write(1, (line + "\n").encode())
        return 0
    if "--imports" in argv:
        return check_imports()
    if "--selftest" in argv:
        import cli
        return cli.main(["selftest"])
    if "--plan" in argv:                              # what is present / missing / outdated, nothing is changed
        import cli
        return cli.main(["plan"] + [a for a in argv if a != "--plan"])
    if "--prepare" in argv:                           # RecompLauncher's setup step: no window, progress lines on stdout (see cli.py)
        import cli
        return cli.main(["prepare"] + [a for a in argv if a != "--prepare"])
    if "--build" in argv:
        import cli
        rest = [a for a in argv if a != "--build"]
        return cli.main(["build"] + rest)
    ap = argparse.ArgumentParser(description="Bravely Emerald builder")
    ap.add_argument("--repo", default=None, help="folder of the repository (default: auto-detect, else asked in the window)")
    ap.add_argument("--dry-run", action="store_true", help="show the commands in the log, execute nothing")
    ap.add_argument("--smoke", action="store_true", help="start, create the window, quit after a second (self test)")
    args = ap.parse_args(argv)
    from core import scale as uiscale
    try:
        from core.settings import load_settings
        classic = os.environ.get("BRAVELY_FONT") == "classic" or (os.environ.get("BRAVELY_FONT") != "system" and bool(load_settings().get("classic_font")))
    except Exception:
        classic = False
    scale_info = uiscale.configure(whole_numbers=classic)
    try:
        from PySide6.QtCore import QTimer
        from PySide6.QtWidgets import QApplication
    except ImportError as e:
        start_failed(e)
        return 1
    from PySide6.QtCore import QObject, Signal, qInstallMessageHandler
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtWidgets import QMessageBox
    from core import crash
    from core.instance import InstanceLock, MESSAGE as ALREADY_RUNNING
    from ui import win95
    from ui.main_window import MainWindow, info_box, TITLE
    app = QApplication(sys.argv[:1])
    app.setApplicationName("Bravely Emerald Builder")
    win95.apply(app)

    class CrashBridge(QObject):                       # lets any thread ask the GUI thread for the message box
        crashed = Signal(str, str)

    bridge = CrashBridge()

    def show_crash(text, path):
        box = info_box(None, "error", "Something went wrong",
                       "Something went wrong and the program has to stop what it was doing.\n\nThe details were saved in:\n%s\n\n"
                       "Press Copy log, then paste it into a new issue at the project page. Nothing was lost: finished build steps are kept."
                       % (path or "(the log could not be saved)"), QMessageBox.Ok)
        copy = box.addButton("Copy log", QMessageBox.ActionRole)
        box.exec()
        if box.clickedButton() is copy:
            QGuiApplication.clipboard().setText(text)

    bridge.crashed.connect(show_crash)
    crash.install(lambda text, path: bridge.crashed.emit(text, path or ""))
    qInstallMessageHandler(crash.qt_message_handler)
    lock = InstanceLock()
    if not lock.acquire():
        info_box(None, "info", TITLE, ALREADY_RUNNING, QMessageBox.Ok).exec()
        return 0
    win = MainWindow(args.repo, args.dry_run)
    win.note(scale_info)
    win.show()
    if args.smoke:
        def finish():
            msg = "SMOKE OK: window %dx%d visible=%s host=%s source=%s" % (win.width(), win.height(), win.isVisible(),
                                                                         win.builder.host["kind"], win.source.mode)
            from core.source import help_file
            msg += " style=%s help=%s" % (type(app.style()).__name__, bool(help_file()))
            png = os.environ.get("BRAVELY_SMOKE_PNG")
            if png:
                win.grab().save(png)
            if sys.stdout is not None:
                print(msg, flush=True)
            else:                       # a windowed (no console) exe has no stdout: write next to the settings file
                from core.settings import app_home
                with open(os.path.join(app_home(), "smoke.txt"), "w", encoding="utf-8") as f:
                    f.write(msg + chr(10))
            win.close()
        QTimer.singleShot(1500, finish)
    return app.exec()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except ImportError as e:                          # a module missing from the install: a message box, not a traceback dialog
        start_failed(e)
        sys.exit(1)
