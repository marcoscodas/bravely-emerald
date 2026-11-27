"""The window of the builder, laid out like the mockup: a frameless Windows 95/98 style window (640x520) with an own title bar,
two sections (ROM, build folder), one consent checkbox and a Build button. While it builds the form is replaced by a progress view.
Everything else is in the Settings dialog and the Help page (the ? button)."""
import os
import sys
import threading
import time

from PySide6.QtCore import QObject, QPoint, QRect, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QGuiApplication, QKeySequence, QPainter, QShortcut, QTextCursor
from PySide6.QtWidgets import (QCheckBox, QFileDialog, QFrame, QHBoxLayout, QLabel, QLineEdit, QMainWindow, QMessageBox, QPlainTextEdit,
                               QProgressBar, QPushButton, QSizePolicy, QStackedWidget, QVBoxLayout, QWidget)

from core import Builder, LEGAL_NOTE, load_settings, save_settings, validate_rom
from core import report
from core.repo import is_repo
from core.source import Source, SourceError
from core.validate import clean_path, cpu_profile, fmt_secs, parts_note, suggest_parts_dir
from . import win95
from .dialogs import HelpDialog, SettingsDialog

TITLE = "Pokemon Bravely Emerald Builder"
CONSENT = "Yeah, download the nerdy stuff needed to build (requires internet and it is safe). *"
LANG = {"E": "USA", "D": "German", "F": "French", "I": "Italian", "S": "Spanish", "J": "Japan", "P": "Europe"}
from core.scale import WINDOW_SIZE
WIDTH, HEIGHT = WINDOW_SIZE

# what the user reads while a stage runs
STEP_TEXT = {"prereq": "Checking this computer...", "install": "Please wait while the build tools are installed...",
             "container": "Please wait while the build environment is prepared...", "files": "Getting the game files...",
             "tools": "Preparing the build tools...", "assets": "Converting the game data...",
             "deps": "Building the game libraries...", "compile": "Compiling the game...", "link": "Compiling the game (last step)...",
             "package": "Packaging the game...", "test": "Testing that the game starts..."}
WEIGHT = {"prereq": 1, "install": 8, "container": 8, "files": 8, "tools": 3, "assets": 12, "deps": 8, "compile": 50, "link": 3,
          "package": 3, "test": 1}

FOOTNOTE = (
    "What the build does with your permission:\n\n"
    "* It downloads the build tools: on Windows MSYS2 (about 85 MB) and the compiler and library packages (several hundred MB) if "
    "they are not installed yet; on Linux and the Steam Deck a build container (about 400 MB) and its compiler packages.\n"
    "* It downloads the SDL2 libraries (a few MB) and the game files from the public pret/pokeemerald project.\n"
    "* The first build takes about 30 to 60 minutes. Later builds are much faster.\n"
    "* It needs about 15 GB of free disk space.\n"
    "* Your ROM is only read to check it. It is never copied or uploaded.\n"
    "* The finished build contains Nintendo's game data. It is for your own use only. Please do not share it.")


class Bridge(QObject):
    """Carries results from worker threads to the GUI thread (queued signals)."""
    event = Signal(dict)
    checks_done = Signal(list)
    source_ready = Signal(str, str)         # repository path, error text
    plan_ready = Signal(object, str)        # (label, stages, steps, meta) or None, error text


def info_box(parent, icon, title, text, buttons, default=None):
    box = QMessageBox(parent)
    box.setWindowTitle(title)
    box.setIconPixmap(win95.flat_icon(icon))
    box.setText(text)
    box.setStandardButtons(buttons)
    if default is not None:
        box.setDefaultButton(default)
    return box


def label(text="", points=10.5, bold=False, wrap=False, color=None):
    lb = QLabel(text)
    lb.setFont(win95.ui_font(1, bold))
    lb.setWordWrap(wrap)
    if color:
        lb.setStyleSheet("color:%s" % color)      # text colour only: the look itself comes from the Windows style
    return lb


class EtchedLine(QWidget):
    """The classic etched line: 1 px dark gray over 1 px white."""

    def __init__(self):
        super().__init__()
        self.setFixedHeight(2)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)

    def paintEvent(self, e):
        p = QPainter(self)
        win95.etched_h(p, 0, 0, self.width())


def sunken_line():
    return EtchedLine()


def section(text):
    """A bold label followed by an etched line that runs to the right edge."""
    row = QHBoxLayout()
    row.setSpacing(10)
    row.addWidget(label(text, 10.5, True))
    row.addWidget(sunken_line(), 1, Qt.AlignVCenter)
    return row


def raised_button(text, bold=False, points=10.5):
    b = QPushButton(text)
    b.setFont(win95.ui_font(1, bold))
    return b


class ClickLabel(QLabel):
    clicked = Signal()

    def mouseReleaseEvent(self, e):
        if e.button() == Qt.LeftButton and self.rect().contains(e.position().toPoint()):
            self.clicked.emit()


class CaptionButton(QWidget):
    """A 16x14 raised caption button with a hand drawn glyph (not a font character)."""
    clicked = Signal()

    def __init__(self, glyph, name):
        super().__init__()
        self.glyph = glyph
        self.down = False
        self.setFixedSize(16, 14)
        self.setToolTip(name)
        self.setAccessibleName(name)
        self.setCursor(Qt.ArrowCursor)

    def paintEvent(self, e):
        p = QPainter(self)
        r = self.rect()
        if self.down:
            win95.pressed(p, r)
        else:
            win95.raised(p, r)
        rows = self.glyph
        gw, gh = len(rows[0]), len(rows)
        off = 1 if self.down else 0
        win95.draw_bitmap(p, (r.width() - gw) // 2 + off, (r.height() - gh) // 2 + off, rows, win95.TEXT)

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            self.down = True
            self.update()

    def mouseReleaseEvent(self, e):
        was = self.down
        self.down = False
        self.update()
        if was and self.rect().contains(e.position().toPoint()):
            self.clicked.emit()


class TitleBar(QWidget):
    """The active caption: solid navy, white bold text, the gem icon at the left, raised ? and X buttons at the right.
    Dragging moves the window; double-click does nothing."""
    HEIGHT = 20

    def __init__(self, win):
        super().__init__()
        self.win = win
        self.setFixedHeight(self.HEIGHT)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 3, 2, 3)
        lay.setSpacing(2)
        lay.addStretch(1)
        self.help_btn = CaptionButton(win95.GLYPH_HELP, "Help")
        self.close_btn = CaptionButton(win95.GLYPH_CLOSE, "Close")
        self.help_btn.clicked.connect(lambda: win.open_help())
        self.close_btn.clicked.connect(win.close)
        lay.addWidget(self.help_btn)
        lay.addSpacing(2)
        lay.addWidget(self.close_btn)

    def paintEvent(self, e):
        p = QPainter(self)
        p.fillRect(self.rect(), win95.NAVY)
        p.drawPixmap(3, 2, win95.gem_pixmap(16))
        p.setPen(win95.HILIGHT)
        p.setFont(win95.ui_font(1, True, pt=9.0))
        box = QRect(24, 0, self.width() - 60, self.height())
        p.drawText(box, Qt.AlignVCenter | Qt.AlignLeft, TITLE)
        if win95.uses_pixel_font():                    # the pixel font has no bold: Win95 smeared the text by one pixel
            p.drawText(box.translated(1, 0), Qt.AlignVCenter | Qt.AlignLeft, TITLE)

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            self.win.begin_drag(e)

    def mouseMoveEvent(self, e):
        if self.win.drag_mode == "manual" and self.win.drag_offset is not None and e.buttons() & Qt.LeftButton:
            self.win.move(e.globalPosition().toPoint() - self.win.drag_offset)

    def mouseReleaseEvent(self, e):
        self.win.drag_offset = None

    def mouseDoubleClickEvent(self, e):
        e.accept()                                     # maximise and the like: nothing happens


class Frame(QWidget):
    """The window body inside the 2 px raised Win95 window border."""

    def paintEvent(self, e):
        p = QPainter(self)
        p.fillRect(self.rect(), win95.FACE)
        win95.window_frame(p, self.rect())


class MainWindow(QMainWindow):
    def __init__(self, repo=None, dry_run=False):
        super().__init__()
        self.setWindowTitle(TITLE)
        self.setWindowIcon(win95.gem_icon())
        self.bridge = Bridge()
        self.bridge.event.connect(self.on_event)
        self.bridge.checks_done.connect(self.on_checks)
        self.bridge.source_ready.connect(self.on_source_ready)
        self.bridge.plan_ready.connect(self.on_plan)
        self.settings = load_settings()
        self.source = Source(force_repo=repo)
        self.source_busy = True
        self.blockers = []
        self.source_error = ""
        self.builder = Builder("", dry_run)
        self.builder.manifest = self.source.manifest
        self.task = None
        self.res = {}
        self.rom_ok = False
        self.stage_order = []
        self.stage_done = set()
        self.cur_stage = None
        self.pct = 0
        self.planning = False
        self.log_buf = []
        self.last_opts = None
        self.no_modal = False          # tests: do not block in message boxes
        self.last_box = None
        self.drag_mode = "system"      # system (startSystemMove), manual (move by mouse) or native (OS frame)
        self.drag_offset = None
        self.native_frame = bool(os.environ.get("BRAVELY_NATIVE_FRAME"))

        flags = Qt.Window if self.native_frame else (Qt.Window | Qt.FramelessWindowHint)
        self.setWindowFlags(flags)
        frame = Frame()
        self.setCentralWidget(frame)
        outer = QVBoxLayout(frame)
        outer.setContentsMargins(2, 2, 2, 2)
        outer.setSpacing(0)
        self.titlebar = TitleBar(self)
        outer.addWidget(self.titlebar)
        if self.native_frame:
            self.titlebar.hide()
            self.note("Using the native window frame (BRAVELY_NATIVE_FRAME is set).")
        self.stack = QStackedWidget()
        outer.addWidget(self.stack, 1)
        self.stack.addWidget(self._form_view())
        self.stack.addWidget(self._progress_view())
        self.stack.addWidget(self._ready_view())
        self.setFixedSize(WIDTH, HEIGHT)
        QShortcut(QKeySequence("Esc"), self, activated=self.close)
        QShortcut(QKeySequence("Alt+F4"), self, activated=self.close)
        QShortcut(QKeySequence.HelpContents, self, activated=lambda: self.open_help())
        self.log_timer = QTimer(self)
        self.log_timer.timeout.connect(self.flush_log)
        self.log_timer.start(150)
        self.clock = QTimer(self)
        self.clock.timeout.connect(self.tick)
        self.clock.start(1000)
        self.rom_edit.setText(self.settings.get("rom", ""))
        if self.rom_edit.text():
            self.check_rom()
        self.check_dir()
        self.update_state()
        QTimer.singleShot(0, self.prepare_source)

    # ------------------------------------------------------------------ frame and dragging
    def note(self, text):
        """A line for the details log (also printed)."""
        self.log_buf.append("# " + text)
        print(text, file=sys.stderr)

    def begin_drag(self, e):
        h = self.windowHandle()
        if self.drag_mode == "system" and h is not None:
            try:
                if h.startSystemMove():
                    return
            except Exception:
                pass
            self.drag_mode = "manual"
            self.note("startSystemMove is not available on this platform (%s); moving the window by mouse instead." % QGuiApplication.platformName())
        if self.drag_mode == "manual":
            if QGuiApplication.platformName().startswith("wayland"):
                self.use_native_frame("This platform does not let a program move its own window (Wayland without move support).")
                return
            self.drag_offset = e.globalPosition().toPoint() - self.frameGeometry().topLeft()

    def use_native_frame(self, why):
        self.drag_mode = "native"
        self.native_frame = True
        self.titlebar.hide()
        self.setWindowFlags(Qt.Window)
        self.show()
        self.note("Falling back to the native window frame: " + why)

    # ------------------------------------------------------------------ views
    def _form_view(self):
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(16, 10, 16, 10)
        lay.setSpacing(0)

        lay.addLayout(section("Select ROM location"))
        lay.addSpacing(4)
        row = QHBoxLayout()
        self.rom_edit = QLineEdit()
        self.rom_edit.setFont(win95.ui_font(1))
        self.rom_edit.setFixedHeight(24)
        self.rom_edit.setAccessibleName("ROM file")
        self.rom_edit.editingFinished.connect(self.check_rom)
        browse = raised_button("Browse")
        browse.setFixedSize(88, 26)
        browse.clicked.connect(self.browse_rom)
        row.addWidget(self.rom_edit, 1)
        row.addSpacing(8)
        row.addWidget(browse)
        lay.addLayout(row)
        self.rom_text = label("", 9, wrap=True)
        self.rom_text.setMinimumHeight(18)
        self.rom_text.setAlignment(Qt.AlignLeft | Qt.AlignTop)
        lay.addWidget(self.rom_text)
        lay.addSpacing(10)

        lay.addLayout(section("Where to build?"))
        lay.addSpacing(4)
        row = QHBoxLayout()
        self.dir_edit = QLineEdit(self.install_dir())
        self.dir_edit.setFont(win95.ui_font(1))
        self.dir_edit.setFixedHeight(24)
        self.dir_edit.setAccessibleName("Build folder")
        self.dir_edit.editingFinished.connect(self.check_dir)
        browse2 = raised_button("Browse")
        browse2.setFixedSize(88, 26)
        browse2.clicked.connect(self.browse_dir)
        row.addWidget(self.dir_edit, 1)
        row.addSpacing(8)
        row.addWidget(browse2)
        lay.addLayout(row)
        self.dir_text = label("", 9, wrap=True)
        self.dir_text.setMinimumHeight(18)
        self.dir_text.setAlignment(Qt.AlignLeft | Qt.AlignTop)
        lay.addWidget(self.dir_text)
        lay.addSpacing(6)

        crow = QHBoxLayout()
        self.consent = QCheckBox()
        self.consent.setAccessibleName(CONSENT)
        self.consent.toggled.connect(lambda _: self.update_state())
        ctext = ClickLabel(CONSENT.replace(" (requires", "<br>(requires"))      # a break where it reads well, not after one word
        ctext.setTextFormat(Qt.RichText)
        ctext.setFont(win95.ui_font(1))
        ctext.setWordWrap(True)
        ctext.clicked.connect(self.consent.toggle)
        crow.addWidget(self.consent, 0, Qt.AlignTop)
        crow.addSpacing(4)
        crow.addWidget(ctext, 1)
        lay.addLayout(crow)
        self.foot = label('<a href="more" style="color:#0000FF">* What is downloaded, how long it takes, and what it needs</a>', 9)
        self.foot.setTextFormat(Qt.RichText)
        self.foot.setTextInteractionFlags(Qt.LinksAccessibleByMouse | Qt.LinksAccessibleByKeyboard)
        self.foot.linkActivated.connect(lambda _: self.show_footnote())
        lay.addSpacing(2)
        lay.addWidget(self.foot)
        lay.addSpacing(4)
        self.problem_text = label("", 9, wrap=True, color="#A00000")
        lay.addWidget(self.problem_text)
        lay.addStretch(1)

        bottom = QHBoxLayout()
        self.settings_link = label('<a href="s" style="color:#0000FF">Settings...</a>', 9.5)
        self.settings_link.setTextFormat(Qt.RichText)
        self.settings_link.setTextInteractionFlags(Qt.LinksAccessibleByMouse | Qt.LinksAccessibleByKeyboard)
        self.settings_link.linkActivated.connect(lambda _: self.open_settings())
        self.build_btn = raised_button("Build", True, 11.5)
        self.build_btn.setFixedSize(104, 28)
        self.build_btn.setDefault(True)
        self.build_btn.clicked.connect(self.start_build)
        bottom.addWidget(self.settings_link, 0, Qt.AlignBottom)
        bottom.addStretch(1)
        bottom.addWidget(self.build_btn)
        lay.addLayout(bottom)
        return w

    def _progress_view(self):
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(16, 12, 16, 10)
        lay.setSpacing(8)
        self.prog_title = label("Building Bravely Emerald", 10.5, True)
        lay.addWidget(self.prog_title)
        self.prog_step = label("", 10.5, wrap=True)
        lay.addWidget(self.prog_step)
        row = QHBoxLayout()
        self.bar = QProgressBar()
        self.bar.setRange(0, 100)
        self.bar.setTextVisible(False)
        self.bar.setFixedHeight(22)
        self.bar.setAccessibleName("Progress")
        self.pct_lbl = label("0%", 10.5)
        self.pct_lbl.setMinimumWidth(38)
        row.addWidget(self.bar, 1)
        row.addWidget(self.pct_lbl)
        lay.addLayout(row)
        self.elapsed = label("Time elapsed: 0:00:00", 10.5)
        lay.addWidget(self.elapsed)
        lay.addWidget(label("This takes a while. You can leave this window open and do something else.", 9.5, wrap=True))

        self.fail_text = label("", 10, wrap=True)
        self.fail_text.setTextFormat(Qt.RichText)
        self.fail_text.setTextInteractionFlags(Qt.TextSelectableByMouse)
        self.fail_box = QFrame()
        self.fail_box.setFrameShape(QFrame.StyledPanel)
        self.fail_box.setFrameShadow(QFrame.Sunken)
        fl = QHBoxLayout(self.fail_box)
        ic = QLabel()
        ic.setPixmap(win95.flat_icon("warning"))
        fl.addWidget(ic, 0, Qt.AlignTop)
        fl.addWidget(self.fail_text, 1)
        self.fail_box.hide()
        lay.addWidget(self.fail_box)

        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setFont(win95.mono_font())
        self.log.setMaximumBlockCount(60000)
        self.log.setAccessibleName("Details")
        self.log.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.log.hide()
        lay.addWidget(self.log, 10)
        lay.addStretch(1)

        row = QHBoxLayout()
        self.details_btn = raised_button("Show details >>")
        self.details_btn.setCheckable(True)
        self.details_btn.toggled.connect(self.toggle_details)
        self.copy_btn = raised_button("Copy log")
        self.copy_btn.clicked.connect(self.copy_log)
        self.retry_btn = raised_button("Try again")
        self.retry_btn.clicked.connect(self.try_again)
        self.back_btn = raised_button("Back")
        self.back_btn.clicked.connect(lambda: self.stack.setCurrentIndex(0))
        self.cancel_btn = raised_button("Cancel")
        self.cancel_btn.clicked.connect(self.cancel)
        for b in (self.details_btn, self.copy_btn, self.retry_btn, self.back_btn, self.cancel_btn):
            b.setMinimumHeight(26)
            b.setMinimumWidth(80)
        row.addWidget(self.details_btn)
        row.addWidget(self.copy_btn)
        row.addStretch(1)
        row.addWidget(self.retry_btn)
        row.addWidget(self.back_btn)
        row.addWidget(self.cancel_btn)
        lay.addLayout(row)
        self.retry_btn.hide()
        self.back_btn.hide()
        return w

    def _ready_view(self):
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(16, 12, 16, 10)
        lay.setSpacing(10)
        head = QHBoxLayout()
        ic = QLabel()
        ic.setPixmap(win95.flat_icon("info"))
        head.addWidget(ic, 0, Qt.AlignTop)
        self.ready_title = label("Your build is ready", 10.5, True)
        head.addWidget(self.ready_title, 1)
        lay.addLayout(head)
        lay.addWidget(label("The game is in this folder:", 10.5))
        self.ready_path = QLineEdit()
        self.ready_path.setFont(win95.ui_font(1))
        self.ready_path.setFixedHeight(24)
        self.ready_path.setReadOnly(True)
        lay.addWidget(self.ready_path)
        row = QHBoxLayout()
        self.open_btn = raised_button("Open folder")
        self.play_btn = raised_button("Play", True)
        self.play_btn.setDefault(True)
        for b in (self.open_btn, self.play_btn):
            b.setFixedSize(104, 28)
        self.open_btn.clicked.connect(lambda: self.open_result("folder"))
        self.play_btn.clicked.connect(lambda: self.open_result("launch"))
        row.addWidget(self.open_btn)
        row.addWidget(self.play_btn)
        row.addStretch(1)
        lay.addLayout(row)
        self.next_lbl = label("", 10.5, wrap=True)
        lay.addWidget(self.next_lbl)
        self.help_btn = raised_button("Help")
        self.help_btn.setFixedSize(88, 26)
        self.help_btn.clicked.connect(lambda: self.open_help("Add it to Steam"))
        lay.addWidget(self.help_btn, 0, Qt.AlignLeft)
        lay.addStretch(1)
        lay.addWidget(label("This build contains Nintendo's game data. It is for your own use only. Please do not share it.", 9.5, wrap=True))
        again = raised_button("Build again")
        again.setFixedSize(88, 26)
        again.clicked.connect(lambda: self.stack.setCurrentIndex(0))
        lay.addWidget(again, 0, Qt.AlignRight)
        return w

    # ------------------------------------------------------------------ boxes
    def run_box(self, box):
        if self.no_modal:
            self.last_box = box
            return QMessageBox.Cancel
        return box.exec()

    def make_footnote_box(self):
        return info_box(self, "info", "What the build does", FOOTNOTE, QMessageBox.Ok)

    def show_footnote(self):
        self.run_box(self.make_footnote_box())

    # ------------------------------------------------------------------ settings access
    def setting(self, key, default=None):
        return load_settings().get(key, default)

    def cpu_level(self):
        v = self.setting("cpu", "low")
        return v if v in ("low", "normal", "high") else "low"

    def install_dir(self):
        return self.setting("dir") or self.builder.default_dir()

    def log_folder(self):
        if self.builder.host["kind"] == "windows":
            return os.path.join(self.builder.repo or self.source.work, "build-win", "builder-logs")
        return os.path.join(self.dir_edit.text().strip() or self.install_dir(), "builder-logs")

    def open_settings(self):
        if self.task and self.task.state == "running":
            self.run_box(info_box(self, "info", "Settings", "Please wait until the build is finished.", QMessageBox.Ok))
            return
        dlg = SettingsDialog(self)
        if dlg.exec():
            save_settings(dlg.values())
            self.update_state()

    def open_help(self, find=None):
        HelpDialog(self, find).exec()

    # ------------------------------------------------------------------ source and checks (background, at start)
    def prepare_source(self, force=False):
        self.source_busy = True
        self.update_state()

        def work():
            try:
                self.bridge.source_ready.emit(self.source.prepare(force), "")
            except (SourceError, OSError) as e:
                self.bridge.source_ready.emit("", str(e))
        threading.Thread(target=work, daemon=True).start()

    def on_source_ready(self, path, err):
        self.source_busy = False
        if err:
            self.builder.repo = ""
            self.source_error = err
            self.update_state()
            return
        self.source_error = ""
        self.builder.repo = os.path.abspath(path)
        self.update_state()
        self.check_rom()
        self.run_checks()

    def run_checks(self):
        def work():
            try:
                res = self.builder.checks()
            except Exception as e:
                res = [{"id": "err", "label": "Check", "status": "bad", "detail": repr(e), "blocking": True}]
            try:
                self.bridge.checks_done.emit(res)
            except RuntimeError:
                pass                                  # the window was closed meanwhile
        threading.Thread(target=work, daemon=True).start()

    def on_checks(self, items):
        self.blockers = [c for c in items if c.get("blocking")]
        self.update_state()

    def choose_work(self):
        d = QFileDialog.getExistingDirectory(self, "Work folder for the program files", os.path.dirname(self.source.work) or os.path.expanduser("~"))
        if d:
            self.source.work = os.path.join(os.path.normpath(d), "source")
            save_settings({"source_dir": self.source.work})
            self.prepare_source()

    def choose_own(self):
        d = QFileDialog.getExistingDirectory(self, "Choose your own checkout of the project", os.path.expanduser("~"))
        if not d:
            return
        if not is_repo(d):
            self.run_box(info_box(self, "error", "Settings", "%s does not hold the build scripts (tools/fetch_assets.sh, tools\\deck, tools\\windows)." % d, QMessageBox.Ok))
            return
        self.source.own = os.path.abspath(d)
        save_settings({"own_checkout": self.source.own})
        self.prepare_source()

    def use_bundled(self):
        self.source.own = ""
        save_settings({"own_checkout": ""})
        self.prepare_source()

    # ------------------------------------------------------------------ ROM and folder fields
    def set_msg(self, lb, text, good=False):
        lb.setText(text)
        lb.setStyleSheet("color:%s" % ("#006400" if good else "#A00000"))

    def browse_rom(self):
        f, _ = QFileDialog.getOpenFileName(self, "Select your Pokemon Emerald ROM", os.path.dirname(self.rom_edit.text()) or os.path.expanduser("~"),
                                           "GBA ROM (*.gba);;All files (*)")
        if f:
            self.rom_edit.setText(os.path.normpath(f))
            self.check_rom()

    def check_rom(self):
        p = self.rom_edit.text().strip()
        if not p:
            self.rom_ok = False
            have = bool(self.builder.repo) and self.builder.have_assets()
            if have:
                self.set_msg(self.rom_text, "The game files of an earlier build were found, so the ROM is not needed again.", True)
            else:
                self.rom_text.setText("")
        else:
            v = validate_rom(p)
            self.rom_ok = v["ok"]
            if v["ok"]:
                self.set_msg(self.rom_text, "Pokemon Emerald found (%s)." % LANG.get(v["code"][3:4], v["code"]), True)
            else:
                self.set_msg(self.rom_text, v["message"])
        self.update_state()

    def browse_dir(self):
        d = QFileDialog.getExistingDirectory(self, "Where to build?", self.dir_edit.text() or os.path.expanduser("~"))
        if d:
            self.dir_edit.setText(os.path.normpath(d))
            self.check_dir()

    def dir_valid(self):
        """(ok, message) for the build folder field."""
        try:
            d = clean_path(self.dir_edit.text(), "The build folder", no_space=False)
        except ValueError as e:
            return False, str(e)
        if self.builder.host["kind"] == "windows" or " " not in d:
            return True, ""
        return True, parts_note(d, suggest_parts_dir(d, self.builder.default_dir()))

    def check_dir(self):
        ok, msg = self.dir_valid()
        self.set_msg(self.dir_text, msg)
        if ok and msg:
            self.dir_text.setStyleSheet("color:#8A4B00")           # a notice, not an error
        self.update_state()

    # ------------------------------------------------------------------ form state
    def update_state(self):
        busy = bool(self.task and self.task.state == "running") or self.planning
        have_files = bool(self.builder.repo) and self.builder.have_assets()
        problem = ""
        if self.source_busy:
            problem = "Please wait while the program files are prepared..."
        elif not self.builder.repo:
            problem = self.source_error or "The program files are not available. Open Settings to fix this."
        elif self.blockers:
            problem = "The build cannot start yet: " + "; ".join("%s. %s" % (c["label"], c["detail"]) for c in self.blockers)
            for c in self.blockers:
                if c.get("manual"):
                    problem += " Run this in a terminal: " + c["manual"]
        self.problem_text.setText(problem)
        self.problem_text.setStyleSheet("color:%s" % ("#000000" if self.source_busy else "#A00000"))      # waiting is not an error
        dir_ok, _ = self.dir_valid()
        ready = (bool(self.builder.repo) and not self.source_busy and not self.blockers and (have_files or self.rom_ok)
                 and dir_ok and self.consent.isChecked())
        self.build_btn.setEnabled(ready and not busy)

    # ------------------------------------------------------------------ build
    def build_opts(self):
        jobs, low = cpu_profile(self.cpu_level(), os.cpu_count() or 1)
        target = "windows" if self.builder.host["kind"] == "windows" else "linux32"
        return {"targets": [target], "rom": self.rom_edit.text().strip(), "dir": self.dir_edit.text().strip(), "jobs": jobs,
                "low_priority": low, "run_test": bool(self.setting("run_test", False)),
                "container": self.setting("container", "emerald-build")}

    def start_build(self):
        opts = self.build_opts()
        self.last_opts = opts
        save_settings({"rom": opts["rom"], "dir": opts["dir"]})
        self.planning = True
        self.update_state()

        def work():
            try:
                self.bridge.plan_ready.emit(self.builder.plan_build(opts), "")
            except ValueError as e:
                self.bridge.plan_ready.emit(None, str(e))
            except Exception as e:
                self.bridge.plan_ready.emit(None, repr(e))
        threading.Thread(target=work, daemon=True).start()

    def on_plan(self, plan, err):
        self.planning = False
        if plan is None:
            self.update_state()
            self.run_box(info_box(self, "error", TITLE, err, QMessageBox.Ok))
            return
        label_, stages, steps, meta = plan
        meta["container"] = (self.last_opts or {}).get("container", "emerald-build")
        try:
            self.task = self.builder.start(label_, stages, steps, meta, "build", lambda ev: self.bridge.event.emit(ev))
        except ValueError as e:
            self.run_box(info_box(self, "error", TITLE, str(e), QMessageBox.Ok))
            return
        self.stage_order = list(stages)
        self.stage_done = set()
        self.cur_stage = None
        self.pct = 0
        self.log.clear()
        self.fail_box.hide()
        self.prog_title.setText("Building Bravely Emerald")
        self.prog_step.setText(STEP_TEXT["prereq"])
        self.set_progress()
        for b, on in ((self.cancel_btn, True), (self.retry_btn, False), (self.back_btn, False)):
            b.setVisible(on)
        self.cancel_btn.setEnabled(True)
        self.stack.setCurrentIndex(1)
        self.update_state()

    def set_progress(self):
        total = sum(WEIGHT.get(s, 1) for s in self.stage_order) or 1
        done = sum(WEIGHT.get(s, 1) for s in self.stage_done)
        cur = 0.0
        if self.cur_stage in self.stage_order:
            w = WEIGHT.get(self.cur_stage, 1)
            cur = w * (self.pct / 100.0 if self.cur_stage == "compile" else 0.3)
        v = int(min(100, 100.0 * (done + cur) / total))
        self.bar.setValue(v)
        self.pct_lbl.setText("%d%%" % v)

    def on_event(self, ev):
        t = ev["type"]
        if t == "log":
            self.log_buf.append(ev["text"])
        elif t == "stage":
            s = ev["stage"]
            if s in self.stage_order:
                self.stage_done.update(self.stage_order[:self.stage_order.index(s)])
                self.cur_stage = s
                self.pct = 0
                self.prog_step.setText(STEP_TEXT.get(s, s))
                self.set_progress()
        elif t == "progress":
            self.pct = ev["pct"]
            self.set_progress()
        elif t == "state":
            self.flush_log()
            self.finish_task(ev)

    def flush_log(self):
        if not self.log_buf:
            return
        sb = self.log.verticalScrollBar()
        at_end = sb.value() >= sb.maximum() - 4
        self.log.appendPlainText("\n".join(self.log_buf))
        self.log_buf = []
        if at_end:
            self.log.moveCursor(QTextCursor.End)

    def finish_task(self, ev):
        st = ev["state"]
        self.cancel_btn.setVisible(False)
        if st == "done":
            self.stage_done.update(self.stage_order)
            self.set_progress()
            self.show_ready(self.task.result or {})
        elif st == "cancelled":
            self.stack.setCurrentIndex(0)
            self.run_box(info_box(self, "info", TITLE, "The build was cancelled. Finished steps are kept, so the next build continues where this one stopped.",
                                  QMessageBox.Ok))
        else:
            self.show_failure(ev)
        self.update_state()

    def show_failure(self, ev):
        hints = ev.get("hints") or []
        html = "<b>A step did not finish.</b>"
        plain = "A step did not finish."
        for h in hints:
            html += "<br><br><b>%s</b><br>%s" % (h["title"], h["fix"])
            plain += "\n\n%s\n%s" % (h["title"], h["fix"])
        if not hints:
            extra = "No known fix matches this problem. Press Show details and read the first error lines."
            html += "<br><br>" + extra
            plain += "\n\n" + extra
        self.fail_text.setText(html)
        self.fail_box.show()
        self.prog_title.setText("The build stopped")
        for b, on in ((self.retry_btn, True), (self.back_btn, True), (self.cancel_btn, False)):
            b.setVisible(on)
        box = info_box(self, "warning", TITLE, plain, QMessageBox.Retry | QMessageBox.Cancel, QMessageBox.Retry)
        if self.run_box(box) == QMessageBox.Retry:
            self.try_again()

    def try_again(self):
        self.start_build()

    def toggle_details(self, on):
        self.log.setVisible(on)
        self.details_btn.setText("<< Hide details" if on else "Show details >>")

    def copy_log(self):
        """The whole report for a GitHub issue: header, plan, full log, FAILED line and probable cause."""
        self.flush_log()
        if self.task:
            QGuiApplication.clipboard().setText(report.full_report(self.task, self.builder, self.source.manifest))
        else:
            QGuiApplication.clipboard().setText(self.log.toPlainText())

    def tick(self):
        t = self.task
        if t and t.state == "running":
            self.elapsed.setText("Time elapsed: " + fmt_secs(time.time() - t.started))

    def cancel(self):
        if not (self.task and self.task.state == "running"):
            return
        box = info_box(self, "question", TITLE, "Do you want to cancel the build?\n\nFinished steps are kept.", QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
        if self.run_box(box) == QMessageBox.Yes:
            self.cancel_btn.setEnabled(False)
            self.builder.cancel()

    # ------------------------------------------------------------------ ready
    def show_ready(self, res):
        self.res = res
        self.ready_path.setText(res.get("folder") or "")
        self.ready_title.setText("Dry run finished" if self.builder.dry_run else "Your build is ready")
        self.open_btn.setEnabled(bool(res.get("ok")))
        self.play_btn.setEnabled(bool(res.get("launch")))
        if res.get("target") == "linux32":
            self.next_lbl.setText("Next step: to play from Steam Game Mode, add the launcher to Steam. Press Help for the steps.")
            self.help_btn.setVisible(True)
        else:
            self.next_lbl.setText("Next step: press Play, or double-click run.bat in that folder.")
            self.help_btn.setVisible(False)
        self.stack.setCurrentIndex(2)

    def open_result(self, what):
        try:
            self.builder.open_result(what)
        except Exception as e:
            self.run_box(info_box(self, "error", TITLE, str(e), QMessageBox.Ok))

    # ------------------------------------------------------------------ closing
    def closeEvent(self, e):
        if self.task and self.task.state == "running":
            box = info_box(self, "question", TITLE, "A build is running. Stop it and quit?\n\nEverything the build started is closed; finished steps are kept.", QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
            if self.run_box(box) != QMessageBox.Yes:
                e.ignore()
                return
            self.builder.cancel()
            for _ in range(50):
                if self.task.state != "running":
                    break
                time.sleep(0.1)
        e.accept()
