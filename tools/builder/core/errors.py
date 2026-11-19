"""Table of known build failures (from docs/BUILD_DECK.md, docs/WINDOWS_BUILD.md and the scripts)."""
import re

# Known failures: pattern in the log -> (title, fix). Sources: docs/BUILD_DECK.md Troubleshooting,
# docs/WINDOWS_BUILD.md, and the messages of the scripts themselves.
KNOWN_ERRORS = [
    (r"No protocol specified|[Cc]an'?t open display|cannot open display|no display|XAUTHORITY",
     "No X display or X authority (typical when started over SSH).",
     "Start the builder from a terminal inside Desktop Mode, or untick the start-up test. The game launcher "
     "sets DISPLAY and XAUTHORITY itself."),
    (r"Python 3 with Pillow is needed|No module named '?PIL",
     "Python Pillow is missing.",
     "Windows: install the 'mingw-w64-x86_64-python-pillow' package (Prerequisites, Install). Linux: the build "
     "installs python-pillow in the container; outside it run 'pip install pillow'."),
    (r"png\.h: No such file|libpng not found",
     "libpng headers were not there yet.",
     "The libraries were still installing or the install was interrupted. Start the build again; it installs what is missing."),
    (r"Generating the port's files.*|No such file or directory.*\.(4bpp|gbapal|lz)|export_assets",
     "The game's graphics were not converted yet (asset ordering).",
     "The fetch script needs the host tools and converted graphics first. The builder runs the steps in the right "
     "order; if you ran tools/fetch_assets.sh by hand, run 'make -f make_tools.mk' and 'make -f vita/assets.mk', then tools/fetch_assets.sh again."),
    (r"not a Pok.mon Emerald dump|header is damaged|does not exist\b",
     "The ROM was rejected.",
     "Use an unmodified dump of Pokemon Emerald (any language, 16 MiB, game code BPE?). Patched or truncated files fail."),
    (r"curl: \(60\)|unable to get local issuer certificate|SSL certificate problem",
     "A download was refused because its certificate is not trusted.",
     "Antivirus 'HTTPS scanning' or a proxy is re-signing the connection. Turn that off for this build (or add github.com "
     "to its exceptions) and run the build again. The build already retries once with the Windows certificate store."),
    (r"no internet|Could not resolve host|Connection timed out|Failed to connect|curl: \(\d+\)|unable to access",
     "A download failed (network or mirror).",
     "Check the internet connection and run the build again. Finished steps are kept."),
    (r"Repository not found|Authentication failed|could not read Username|terminal prompts disabled|Permission denied \(publickey\)",
     "The repository could not be cloned (it is private for now).",
     "Clone it yourself with credentials that have access (git clone https://github.com/marcoscodas/bravely-emerald), "
     "then use 'Choose folder' to point the app at the clone."),
    (r"checksum mismatch",
     "A downloaded file does not match its published checksum.",
     "Run the build again so the file is fetched anew. If it repeats, report it; do not continue with the file."),
    (r"mingw-w64-i686-toolchain missing|pacman -S cmake",
     "The MSYS2 toolchain packages are missing.",
     "Prerequisites panel, 'MSYS2 packages', Install."),
    (r"repository path contains a space|install dir contains a space|ROM path contains a space",
     "A path contains a space.",
     "Move the clone, the install folder or the ROM to a path without spaces."),
    (r"less than \d+ MB free",
     "Not enough free disk space.",
     "Free some space or choose another install folder."),
    (r"distrobox not found|podman not found",
     "distrobox or podman is missing.",
     "SteamOS ships both. On another distribution install them with the package command in the Prerequisites panel."),
    (r"sudo: a password is required|sudo: no tty present|a terminal is required",
     "A command needs your administrator password.",
     "The builder never asks for or stores passwords. Run the command shown in the log in a terminal yourself, then press Start build again."),
    (r"[Pp]ermission denied",
     "Permission denied.",
     "Run the scripts with bash as the builder does, and check that the folders are writable by your user."),
    (r"error: .*\.c:\d+|Error \d+$|make: \*\*\*",
     "The compiler or make reported an error.",
     "Press Start build again: finished files are kept and often a leftover from an interrupted run is the cause. "
     "If the same error repeats, copy the first error lines of the log into a bug report."),
]

CONTAINER_ERRORS = [
    (r"\binstalling\s+(?:lib32-)?openssl(?:\s+\([^\n)]+\))?\s+breaks\s+dependency\b|"
     r"\b(?:breaks\s+dependency|could not satisfy\s+dependency|requires)\s+['\"]?"
     r"(?:(?:libcrypto|libssl)\.so\b|openssl[=<>])",
     "Arch Linux reported an OpenSSL dependency conflict in the build container.",
     "The installed packages and Arch repositories need to be in sync. See OpenSSL in the build guide's "
     "Troubleshooting section."),
    (r"\bchpasswd:[^\n]*(?:\n[ \t]*)*invalid password hash\b",
     "Distrobox could not set up user access (chpasswd: invalid password hash).",
     "Check 'distrobox --version'. Distrobox 1.8.2.5 resolved this error on Linux Mint 22.3. "
     "Use a version that includes the fix for newer shadow-utils."),
    (r"(?:\b(?:podman|crun|runc|newuidmap|newgidmap)\b|containers/(?:storage|overlay))[^\n]*"
     r"(?:permission denied|operation not permitted)|\bOCI permission denied\b|"
     r"(?:permission denied|operation not permitted)[^\n]*\bpodman\b",
     "Podman or its OCI runtime reported a container permission error.",
     "Check the reported path and your user's rootless Podman permissions. If the error points to external "
     "storage on a Steam Deck, check the Podman storage location in the build guide."),
    (r"\b(?:connection|TLS handshake) timed out\b|\bi/o timeout\b|\boperation too slow\b|"
     r"\bcurl:\s*\(28\)|\bclient\.timeout exceeded\b|(?:download|retrieving file)[^\n]*\b(?:timed out|timeout)\b",
     "A network operation timed out while setting up the build container.",
     "Check the connection to the image registry or Arch package mirror and try again."),
    (r"\bcould not resolve host\b|\bfailed to connect\b|\bconnection reset\b",
     "A network connection failed while setting up the build container.",
     "Check the connection and name resolution for the image registry or package mirror and try again."),
]

ANSI_ESCAPE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def container_hint(lines):
    """Return a fixed diagnosis without copying any content from the container log."""
    text = ANSI_ESCAPE.sub("", "\n".join(lines))
    text = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("+"))
    for pat, title, fix in CONTAINER_ERRORS:
        if re.search(pat, text, re.I):
            return {"title": title, "fix": fix}
    return None


def explain(lines):
    """Match the tail of a log against the table of known errors."""
    text = "\n".join(lines[-300:])
    hits = []
    for pat, title, fix in KNOWN_ERRORS:
        if re.search(pat, text, re.M):
            hits.append({"title": title, "fix": fix})
        if len(hits) >= 3:
            break
    return hits
