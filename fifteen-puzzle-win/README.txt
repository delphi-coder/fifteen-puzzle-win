Fifteen Puzzle for Windows
==========================

Fifteen Puzzle is a small native Windows implementation of the classic 15-puzzle game.

The program is written in C and uses the Win32 API directly. It is intended as a simple educational example for people who want to learn how a traditional Windows GUI program works without using a large framework or game engine.

This updated version includes a more complete set of game features such as undo, redo, save, save as, load, load from, pause/resume, new game, and restart.

Project overview
----------------
The game uses a 4x4 board with fifteen numbered tiles and one blank space. The player moves tiles into the blank space until all numbers are arranged in order.

The program shows:
- Move count
- Elapsed time
- A simple menu bar
- Custom owner-drawn tile buttons
- Save/load support
- Undo/redo history
- Restart support for replaying the same shuffled puzzle

Main features
-------------
- Classic 15-puzzle gameplay
- New random game
- Restart current puzzle
- Pause and resume
- Undo and redo
- Save to the default game.15 file
- Load from the default game.15 file
- Save As using the standard Windows file dialog
- Load From using the standard Windows file dialog
- Compact binary save-file format
- Solvable shuffle generation
- Owner-drawn buttons for a cleaner tile appearance
- Keyboard shortcuts for common actions

Keyboard shortcuts
------------------
Ctrl+S          Save
Ctrl+Shift+S    Save As
Ctrl+O          Load
Ctrl+Shift+O    Load From
Ctrl+Z          Undo
Ctrl+Y          Redo

Build environment
-----------------
This project is designed for Visual C++ 2010.

Open the solution file:

    fifteen-puzzle-win.sln

Then build either the Debug or Release Win32 configuration.

Important source files
----------------------
fiftten-puzzle.c
    Main application source file. It contains the Win32 entry point, window procedure,
    board logic, drawing code, save/load logic, timer handling, and undo/redo logic.

res.rc
    Resource script used by the Visual Studio project.

resource.h
    Resource identifiers.

icon1.ico
    Application icon.

app.manifest
    Application manifest.

How the program works
---------------------
The application starts in wWinMain, registers a Win32 window class, creates the main window, and then enters the standard Windows message loop.

The window procedure handles messages such as:

- WM_CREATE for creating menus, labels, tile buttons, fonts, and the first game
- WM_SIZE for recalculating the board layout
- WM_COMMAND for menu commands and tile clicks
- WM_TIMER for updating elapsed time
- WM_DRAWITEM for custom drawing of tile buttons and status labels
- WM_DESTROY for cleanup

The puzzle board is stored as an array of 16 integers. Values 1 to 15 represent the numbered tiles and value 0 represents the blank space.

The shuffle code avoids impossible puzzle states by checking the inversion count and the blank row position. This is important because not every random arrangement of a 15-puzzle can be solved.

Save-file format
----------------
The program saves the current board, the original starting board, the random seed, move count, elapsed time, and state flags.

Each board is packed into 8 bytes because every tile value fits in 4 bits. This keeps the save file small and simple.

Notes
-----
The source code is heavily commented to make it easier to study.

This is not intended to be a commercial game. It is a compact learning project that demonstrates practical Win32 programming concepts using plain C.

Suggested package contents
--------------------------
README.txt
fifteen-puzzle-win.sln
fifteen-puzzle-win.vcxproj
fifteen-puzzle-win.vcxproj.filters
fiftten-puzzle.c
res.rc
resource.h
icon1.ico
app.manifest
bin/fifteen-puzzle-win.exe
screenshots/fifteen-puzzle.png
CLEANUP_NOTES.txt
