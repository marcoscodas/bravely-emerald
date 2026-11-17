Pokemon Emerald, voxel port, Windows version
============================================

Start the game: double-click run.bat (or pokeemerald.exe). Keep the folders app0\ and userdata\
next to the exe; the game looks for them in its own folder, whatever folder you start it from.

Needs: Windows 10 or later, a graphics driver with OpenGL 2.1 or newer (GLSL 1.20 for the
effects). 32-bit program, runs on 64-bit Windows.

Window: starts in a 1280x720 window you can resize. F11 or Alt+Enter switch to fullscreen and
back at any time. START > OPTIONS > GENERAL > DISPLAY has SCREEN MODE (windowed or fullscreen) and
RESOLUTION (window size, or the screen mode in fullscreen); both are remembered in
userdata\settings.txt (keys fullscreen and resolution). Setting the environment variable
DECK_FULLSCREEN=1 (or 0) forces the first start into fullscreen (or a window).

Controls: keyboard and any game controller SDL supports. Keyboard: arrows move, Z = A, X = B,
Enter = START, \ = SELECT, A = L, S = R, F11 or Alt+Enter = fullscreen, Space = fast forward. The settings menu of the port (WORLD >
EFFECTS) switches every visual effect on and off.

Saves and settings (all in userdata\):
  userdata\saves\pokeemerald.sav   the save game
  userdata\settings.txt            port settings
  userdata\debug.log, gl_errors.log, stderr.txt   diagnostics (safe to delete)

This folder ships with an empty save. To continue a Steam Deck save, close the game and copy the
file userdata/saves/pokeemerald.sav from the Deck install (folder build-deck/dist/) into
userdata\saves\ here, replacing any existing one.

Diagnostics for bug reports: set DECK_GL_DEBUG=1 and look at userdata\gl_errors.log.
