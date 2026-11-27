"""Input validation: paths, the ROM check (mirror of tools/fetch_assets.sh), disk, network."""
import hashlib
import os
import re
import shutil
import socket

from .constants import ROM_SIZE, ROM_TITLE, EMERALD_US_SHA1


# ---------------------------------------------------------------------------------------------
def clean_path(p, what="path", allow_empty=False, no_space=True):
    """Validate a path received from the UI. Returns the normalized string or raises ValueError."""
    if not isinstance(p, str):
        raise ValueError("%s must be a string" % what)
    p = p.strip().strip('"')
    if not p:
        if allow_empty:
            return ""
        raise ValueError("%s is empty" % what)
    if len(p) > 400 or any(ord(c) < 32 for c in p):
        raise ValueError("%s has invalid characters" % what)
    if re.search(r"[\"'`$;&|<>*?!]", p):
        raise ValueError("%s contains characters the build scripts cannot handle (quotes or shell symbols)" % what)
    if no_space and " " in p:
        raise ValueError("%s contains a space; the build scripts need a path without spaces" % what)
    if not os.path.isabs(os.path.expanduser(p)):
        raise ValueError("%s must be an absolute path" % what)
    return os.path.normpath(os.path.expanduser(p))


def parts_note(d, parts):
    """What to tell the player when the Linux install folder has a space and the build therefore goes elsewhere."""
    return ("This folder has a space in its name (%s), where the build cannot run. The build tools, compiled files and game "
            "files go in %s instead. The launcher, saves and settings stay in your folder. Do not move or delete either one."
            % (d, parts))


def _existing(path):
    """`path`, or the nearest folder above it that exists."""
    while not os.path.exists(path) and os.path.dirname(path) != path:
        path = os.path.dirname(path)
    return path


def _drive(path):
    return os.stat(path).st_dev


def suggest_parts_dir(d, default_dir):
    """The folder for the build parts of the install folder `d`, which has a space. It sits above the first space, when that
    is writable and on d's drive; otherwise next to the default install folder. One folder per install folder, so two
    installs never share a build. Same choice as auto_parts_dir in tools/deck/build_deck.sh."""
    tag = "-parts-" + hashlib.sha1(d.encode("utf-8")).hexdigest()[:8]
    above = d
    while " " in above:
        above = os.path.dirname(above)
    suggestion = os.path.join(above, "bravely-emerald" + tag)
    there = _existing(suggestion)
    if _drive(there) == _drive(_existing(d)) and os.access(there, os.W_OK):
        return suggestion
    return default_dir + tag


def source_path_problem(path, windows=True):
    """Plain-language reason why the build scripts cannot work in this folder, or None. Checked before anything starts.
    Unsupported: a space (make cannot handle it), letters outside plain ASCII (the MinGW tools read the path with the
    ANSI code page), a network path, a path so long that Windows file names go over 260 characters, shell symbols."""
    if " " in path:
        return ("The source folder path has a space in it (%s). The build tools cannot handle spaces: use a folder such as "
                "C:\\BravelyEmerald\\source." % path)
    if any(ord(c) > 126 for c in path):
        return ("The source folder path has letters outside plain English letters, such as an accent (%s). The compiler tools "
                "cannot read them: use a folder such as C:\\BravelyEmerald\\source." % path)
    if windows and (path.startswith("\\\\") or path.startswith("//")):
        return "The source folder is on a network path (%s). Build on a local drive, for example C:\\BravelyEmerald\\source." % path
    if re.search(r"[\"'`$;&|<>*?!]", path):
        return "The source folder path has quotes or symbols the build scripts cannot handle (%s)." % path
    if len(path) > 110:
        return "The source folder path is %d characters long; keep it under 110 so Windows file names stay under 260." % len(path)
    return None


def validate_rom(path):
    """Mirror of the check in tools/fetch_assets.sh (size, title, game code, header checksum, SHA-1).
    Returns dict(ok, code, message, level). Reads the file only; never copies it."""
    try:
        path = clean_path(path, "ROM path", no_space=False)
    except ValueError as e:
        return {"ok": False, "message": str(e)}
    if not os.path.isfile(path):
        return {"ok": False, "message": "File not found: %s" % path}
    try:
        size = os.path.getsize(path)
        if size != ROM_SIZE:
            return {"ok": False, "message": "Not an Emerald dump: size is %d bytes, expected %d (16 MiB). "
                    "A truncated, trimmed or different file." % (size, ROM_SIZE)}
        with open(path, "rb") as f:
            f.seek(0xA0)
            head = f.read(0x20)          # 0xA0 .. 0xBF
            f.seek(0)
            h = hashlib.sha1()
            while True:
                chunk = f.read(1 << 20)
                if not chunk:
                    break
                h.update(chunk)
    except OSError as e:
        return {"ok": False, "message": "Cannot read the file: %s" % e}
    title = head[0:12].rstrip(b"\0")
    code = head[12:16]
    if title != ROM_TITLE:
        return {"ok": False, "message": "Not Pokemon Emerald: the header title is %r." % title.decode("latin-1")}
    if not (code[:3] == b"BPE" and len(code) == 4):
        return {"ok": False, "message": "Not Pokemon Emerald: the game code is %r, expected BPE?." % code.decode("latin-1")}
    total = sum(head[0:29])                      # bytes 0xA0..0xBC
    expected = (0 - (total + 25)) & 255
    if expected != head[29]:                     # byte 0xBD
        return {"ok": False, "message": "The ROM header checksum is wrong: the dump is modified or damaged."}
    sha = h.hexdigest()
    if sha == EMERALD_US_SHA1:
        return {"ok": True, "code": code.decode(), "message": "Pokemon Emerald (%s), USA/Europe 1.0: verified (SHA-1 matches)." % code.decode(),
                "sha1_match": True}
    return {"ok": True, "code": code.decode(), "message": "Pokemon Emerald (%s): header verified. Another language or revision "
            "than USA/Europe 1.0; any language works." % code.decode(), "sha1_match": False}


def free_gb(path):
    probe = path
    while probe and not os.path.exists(probe):
        nxt = os.path.dirname(probe)
        if nxt == probe:
            break
        probe = nxt
    return shutil.disk_usage(probe or ".").free / (1024 ** 3)


def network_ok():
    try:
        socket.create_connection(("github.com", 443), timeout=6).close()
        return True
    except OSError:
        return False

def fmt_secs(s):
    s = int(s)
    return "%d:%02d:%02d" % (s // 3600, s % 3600 // 60, s % 60)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def cpu_profile(level, cpus):
    """(jobs, low_priority) for the CPU usage setting: low keeps the PC usable, high uses every thread."""
    cpus = max(1, int(cpus or 1))
    if level == "high":
        return cpus, False
    if level == "normal":
        return max(1, cpus * 3 // 4), False
    return max(1, cpus // 2), True
