/*
    Fifteen Puzzle for Windows
    ==========================

    This is a small native Win32 implementation of the classic 15-puzzle game.

    The program intentionally uses the Windows API directly instead of a GUI
    framework such as MFC, .NET, Qt, or a game engine. For that reason it is a
    useful learning sample for beginners who want to see how a small Windows GUI
    program is built from the lower-level building blocks:

      - Registering a window class.
      - Creating the main window.
      - Creating menus and accelerator keys.
      - Handling the message loop.
      - Creating child controls.
      - Drawing custom controls through WM_DRAWITEM.
      - Using timers for elapsed time.
      - Saving and loading compact binary game state.
      - Keeping undo/redo history in memory.

    The code is still deliberately compact, but the comments explain the main
    ideas so the project can be studied more easily.
*/

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
#include <tchar.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "resource.h"
/* -------------------------------------------------------------------------
   Application constants
   -------------------------------------------------------------------------
   GRID_SIZE is fixed at 4, which gives a 4x4 board.
   The board contains 15 numbered tiles plus one empty space.

   PADDING is the small spacing between tiles.
   TOP_ROW_H reserves the top part of the client area for the Moves and Time
   labels. The grid itself begins below this row.
------------------------------------------------------------------------- */
#define GRID_SIZE   4
#define TILE_COUNT  (GRID_SIZE*GRID_SIZE)
#define PADDING     1
#define TOP_ROW_H   32

#define APP_CLASS   L"FifteenGtkWinOwnerDraw"
#define APP_TITLE   L"Fifteen Puzzle"

/* -------------------------------------------------------------------------
   Menu and control identifiers
   -------------------------------------------------------------------------
   Win32 sends WM_COMMAND messages with numeric command/control IDs. These IDs
   let the window procedure know whether the user clicked New, Save, Undo, a
   tile button, and so on.

   ID_TILE_BASE is the first ID used for the 16 tile buttons. The buttons use
   IDs 4000..4015, so the clicked board index can be calculated by subtracting
   ID_TILE_BASE from the received command ID.
------------------------------------------------------------------------- */
#define IDM_FILE_NEW        1001
#define IDM_FILE_RESTART    1002
#define IDM_FILE_PAUSE_RES  1003
#define IDM_FILE_SAVE       1004
#define IDM_FILE_LOAD       1005
#define IDM_FILE_EXIT       1006
#define IDM_EDIT_UNDO       1007
#define IDM_EDIT_REDO       1008
#define IDM_HELP_ABOUT      2001

#define IDT_ONESEC          3001
#define ID_TILE_BASE        4000   /* 4000..4015 tile buttons */

/* The Moves and Time labels are owner-drawn static controls. They could be
   ordinary STATIC controls, but owner-drawing keeps the visual style consistent
   with the custom drawn tile buttons. */
#define ID_LBL_MOVES        5001
#define ID_LBL_TIME         5002

/* -------------------------------------------------------------------------
   Global program state
   -------------------------------------------------------------------------
   This is a single-window demo-style program, so keeping the state as globals
   keeps the code simple.

   g_pos:
       Current board layout. Values 1..15 are tile numbers, value 0 is the
       empty cell.

   g_start_pos:
       The initial shuffled layout for the current game. Restart uses this array
       so the player can replay the same puzzle from the beginning.

   g_blank:
       Current index of the empty cell in g_pos.

   g_moves / g_elapsed:
       Player statistics shown at the top of the window.

   g_paused / g_solved:
       Control whether the timer and tile input should be active.
------------------------------------------------------------------------- */
static HWND   g_hwnd = NULL;
static HWND   g_lblMoves = NULL;
static HWND   g_lblTime  = NULL;
static HWND   g_tiles[TILE_COUNT];

static int    g_pos[TILE_COUNT];    /* 1..15, 0=blank */
static int    g_start_pos[TILE_COUNT];
static int    g_blank = TILE_COUNT-1;
static int    g_start_blank = TILE_COUNT-1;
static unsigned int g_moves = 0;
static BOOL   g_paused = FALSE;
static BOOL   g_solved = FALSE;

static HMENU  g_hMenuMain = NULL;
static HMENU  g_hFileMenu = NULL;
static HACCEL g_accel = NULL;

static UINT   g_timer = 0;
static time_t g_start_time = 0;
static int    g_elapsed = 0;        /* seconds */
static unsigned int g_seed = 0;

/* -------------------------------------------------------------------------
   Undo / redo history
   -------------------------------------------------------------------------
   The application stores snapshots of the complete game state. This is simple,
   reliable, and very easy to understand.

   HISTORY_MAX limits memory usage and also prevents the undo list from growing
   forever. A 4x4 board is tiny, so keeping 512 snapshots is still very small.
------------------------------------------------------------------------- */
#define HISTORY_MAX 512
typedef struct GameState {
    int pos[TILE_COUNT];
    int blank;
    unsigned int moves;
    int elapsed;
    BOOL paused;
    BOOL solved;
} GameState;
static GameState g_history[HISTORY_MAX];
static int g_history_count = 0;
static int g_history_index = 0;

static RECT   g_gridRect;           /* client area reserved for grid */
static int    g_cellW = 0, g_cellH = 0;

static HFONT  g_uiFont = NULL;      /* UI font for labels/buttons */

/* ===== Forward decls ===== */
static void layout_children(void);
static void apply_board_to_buttons(void);
static void enable_tiles(BOOL en);
static void do_new_game(void);
static void do_restart_game(void);
static void do_pause_resume(void);
static void do_save_game(BOOL save_as);
static void do_load_game(BOOL from_file);
static void do_undo(void);
static void do_redo(void);
static void do_about(void);
static void try_click_index(int idx);
static void set_moves_label(void);
static void set_time_label(void);

/* -------------------------------------------------------------------------
   Save-file format
   -------------------------------------------------------------------------
   The save file stores the board in a compact binary format.

   Each tile value fits in 4 bits because the values are 0..15. Therefore a full
   16-cell board can be stored in 8 bytes. The current board and the original
   starting board are both saved, so a loaded game can still be restarted.

   #pragma pack(push, 1) prevents the compiler from inserting padding bytes
   inside the structure. That makes the file format small and predictable.
------------------------------------------------------------------------- */
#pragma pack(push, 1)
typedef struct GameSaveData {
    DWORD signature;
    DWORD version;
    BYTE pos_packed[8];
    BYTE start_packed[8];
    DWORD seed;
    DWORD moves;
    DWORD elapsed;
    BYTE flags;
} GameSaveData;
#pragma pack(pop)

#define SAVE_SIGNATURE 0x31504649u /* "IFP1" */
#define SAVE_VERSION   1u

/* -------------------------------------------------------------------------
   Board logic
   -------------------------------------------------------------------------
   These functions do not depend on Windows. They only work with arrays that
   represent the puzzle board.

   A 15-puzzle position is not always solvable. The shuffle code must generate
   only valid positions; otherwise the player could receive a puzzle that looks
   normal but can never be solved.
------------------------------------------------------------------------- */
/* Fill a board array with the solved position:
   1  2  3  4
   5  6  7  8
   9 10 11 12
  13 14 15  0
   The zero represents the blank space. */
static void new_solved(int* a){ int i; for(i=0;i<TILE_COUNT-1;i++) a[i]=i+1; a[TILE_COUNT-1]=0; }
/* Count inversions in the board. An inversion is a pair of numbered tiles that
   appear in the opposite order compared to the solved state. The parity of this
   count is part of the 15-puzzle solvability rule. */
static int inv_count(const int* a){ int inv=0,i,j; for(i=0;i<TILE_COUNT;i++) if(a[i]) for(j=i+1;j<TILE_COUNT;j++) if(a[j]&&a[i]>a[j]) inv++; return inv; }
/* Return the blank row counted from the bottom of the board. For a 4x4 puzzle,
   this value is needed together with the inversion count to decide whether the
   board can be solved. */
static int blank_row_from_bottom(const int* a){ int i; for(i=0;i<TILE_COUNT;i++) if(a[i]==0){ int r=i/GRID_SIZE; return GRID_SIZE-r; } return 0; }
/* Check the mathematical solvability of a board position.
   For even-width boards such as 4x4, the blank row from the bottom changes the
   parity rule. This prevents generating or loading impossible puzzles. */
static BOOL is_solvable(const int* a){
    int inv = inv_count(a);
    if ((GRID_SIZE%2)==1) return (inv%2)==0;
    { int rb=blank_row_from_bottom(a); return (rb%2)==0 ? (inv%2)==1 : (inv%2)==0; }
}
/* Check whether the current board exactly matches the solved layout. */
static BOOL is_solved_positions(const int* a){
    int i; for(i=0;i<TILE_COUNT-1;i++) if(a[i]!=i+1) return FALSE; return a[TILE_COUNT-1]==0;
}
/* Validate that a board contains every value from 0 to 15 exactly once. This is
   important when loading a saved file because external files cannot be trusted. */
static BOOL is_valid_board(const int* a){
    int seen[TILE_COUNT];
    int i;
    for (i = 0; i < TILE_COUNT; i++) seen[i] = 0;
    for (i = 0; i < TILE_COUNT; i++){
        int v = a[i];
        if (v < 0 || v >= TILE_COUNT) return FALSE;
        if (seen[v]) return FALSE;
        seen[v] = 1;
    }
    return TRUE;
}
/* Pack sixteen 4-bit tile values into eight bytes.
   Low nibble  = first tile
   High nibble = second tile */
static void pack_board(const int* board, BYTE* packed){
    int i;
    for (i = 0; i < 8; i++) packed[i] = (BYTE)(board[i * 2] | (board[i * 2 + 1] << 4));
}
/* Reverse of pack_board(): expand the compact 8-byte representation back into
   sixteen integer tile values. */
static void unpack_board(const BYTE* packed, int* board){
    int i;
    for (i = 0; i < 8; i++){
        board[i * 2] = packed[i] & 0x0F;
        board[i * 2 + 1] = (packed[i] >> 4) & 0x0F;
    }
}
/* Shuffle the board using Fisher-Yates style swapping, then reject the result
   if it is either unsolvable or already solved. */
static void shuffle_positions(int* pos){
    int ok=0;
    while(!ok){
        int i;
        for(i=TILE_COUNT-1;i>0;i--){
            int j = rand() % (i+1);
            int t = pos[i]; pos[i]=pos[j]; pos[j]=t;
        }
        ok = is_solvable(pos) && !is_solved_positions(pos);
    }
}

/* -------------------------------------------------------------------------
   Label updates
   -------------------------------------------------------------------------
   The label controls are owner-drawn. Updating their text is done by invalidating
   them, which tells Windows to repaint them. The actual text is generated in
   draw_info_label().
------------------------------------------------------------------------- */
static void set_moves_label(void){
    InvalidateRect(g_lblMoves, NULL, TRUE);
}
static void set_time_label(void){
    InvalidateRect(g_lblTime, NULL, TRUE);
}

/* Enable or disable Undo/Redo menu items according to the current history position. */
static void update_edit_menu(void){
    if (g_hMenuMain){
        EnableMenuItem(g_hMenuMain, IDM_EDIT_UNDO, MF_BYCOMMAND | (g_history_index > 0 ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(g_hMenuMain, IDM_EDIT_REDO, MF_BYCOMMAND | (g_history_index + 1 < g_history_count ? MF_ENABLED : MF_GRAYED));
        DrawMenuBar(g_hwnd);
    }
}

/* Change the Pause menu caption to Resume when the game is paused, and back to Pause when it is running. */
static void set_menu_pause_label(void){
    if (g_hFileMenu){
        ModifyMenuW(g_hFileMenu, IDM_FILE_PAUSE_RES, MF_BYCOMMAND|MF_STRING, IDM_FILE_PAUSE_RES,
                    g_paused ? L"Resume" : L"Pause");
    }
}

/* Build the default save-file path next to the executable: game.15. */
static void get_default_save_path(wchar_t* path, DWORD cchPath){
    DWORD n = GetModuleFileNameW(NULL, path, cchPath);
    wchar_t* slash;
    if (!n || n >= cchPath){ lstrcpynW(path, L"game.15", cchPath); return; }
    slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = 0;
    lstrcatW(path, L"game.15");
}

/* Display the standard Windows Save As dialog for .15 save files. */
static BOOL choose_save_path(wchar_t* path, DWORD cchPath){
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    get_default_save_path(path, cchPath);
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFile = path;
    ofn.nMaxFile = cchPath;
    ofn.lpstrFilter = L"Fifteen Puzzle Save (*.15)\0*.15\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"15";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    return GetSaveFileNameW(&ofn);
}

/* Display the standard Windows Open dialog for .15 save files. */
static BOOL choose_load_path(wchar_t* path, DWORD cchPath){
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    get_default_save_path(path, cchPath);
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFile = path;
    ofn.nMaxFile = cchPath;
    ofn.lpstrFilter = L"Fifteen Puzzle Save (*.15)\0*.15\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    return GetOpenFileNameW(&ofn);
}

/* Restore the visible board, timer, menu state, and controls from a saved history snapshot. */
static void set_game_state_from_state(const GameState* s){
    int i;

    for (i = 0; i < TILE_COUNT; i++){
        g_pos[i] = s->pos[i];
    }
    g_blank = s->blank;
    g_moves = s->moves;
    g_elapsed = s->elapsed;
    g_paused = s->paused ? TRUE : FALSE;
    g_solved = s->solved ? TRUE : FALSE;

    apply_board_to_buttons();
    enable_tiles(!g_paused);

    if (g_timer){ KillTimer(g_hwnd, IDT_ONESEC); g_timer = 0; }
    if (!g_paused && !g_solved){
        g_start_time = time(NULL) - g_elapsed;
        g_timer = SetTimer(g_hwnd, IDT_ONESEC, 1000, NULL);
    }

    set_menu_pause_label();
    update_edit_menu();
    DrawMenuBar(g_hwnd);
}

/* Copy the current global game state into one history snapshot. */
static void capture_state(GameState* s){
    int i;
    for (i = 0; i < TILE_COUNT; i++) s->pos[i] = g_pos[i];
    s->blank = g_blank; s->moves = g_moves; s->elapsed = g_elapsed;
    s->paused = g_paused; s->solved = g_solved;
}
/* Start a new undo/redo chain from the current board state. */
static void history_reset(void){
    capture_state(&g_history[0]);
    g_history_count = 1; g_history_index = 0; update_edit_menu();
}
/* Add a new state to the undo/redo history after a successful move. */
static void history_record(void){
    if (g_history_index + 1 < HISTORY_MAX){
        g_history_index++; g_history_count = g_history_index + 1;
    }else{
        memmove(&g_history[0], &g_history[1], sizeof(GameState) * (HISTORY_MAX - 1));
        g_history_index = HISTORY_MAX - 1; g_history_count = HISTORY_MAX;
    }
    capture_state(&g_history[g_history_index]); update_edit_menu();
}

/* -------------------------------------------------------------------------
   Owner-drawn tile buttons
   -------------------------------------------------------------------------
   Normal BUTTON controls use the operating system theme. This application uses
   BS_OWNERDRAW so the tiles can have a soft, custom, GTK-like visual style while
   still behaving like standard Win32 buttons.

   WM_DRAWITEM calls draw_tile_button() whenever a tile needs repainting.
------------------------------------------------------------------------- */
/* Select colors for a tile depending on its visual state. */
static void gtkish_colors(BOOL disabled, BOOL pressed, COLORREF* fill, COLORREF* border, COLORREF* text){
    if (disabled){ *fill=RGB(238,238,238); *border=RGB(200,200,200); *text=RGB(160,160,160); return; }
    if (pressed) { *fill=RGB(208,214,230); *border=RGB(115,130,170); *text=RGB(30,30,30);   return; }
    *fill=RGB(230,235,245); *border=RGB(130,145,185); *text=RGB(30,30,30);
}
/* Helper for drawing a filled rounded rectangle with a one-pixel border. */
static void draw_round_rect(HDC hdc, RECT rc, COLORREF fill, COLORREF border, int rad){
    HBRUSH b = CreateSolidBrush(fill);
    HPEN   p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(hdc, b);
    HGDIOBJ op = SelectObject(hdc, p);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, rad, rad);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(b); DeleteObject(p);
}
/* Draw one tile button: background, rounded tile body, focus rectangle, and centered number. */
static void draw_tile_button(DRAWITEMSTRUCT* dis){
    BOOL focused;
    BOOL disabled;
    BOOL pressed;
    wchar_t text[16];
    int n;
    HBRUSH bg;
    RECT r;
    COLORREF fill, border, fnt;

    focused  = (dis->itemState & ODS_FOCUS)!=0;
    disabled = (dis->itemState & ODS_DISABLED)!=0;
    pressed  = (dis->itemState & ODS_SELECTED)!=0;

    n = GetWindowTextW(dis->hwndItem, text, 15);
    if (n<0) n=0;
    text[n]=0;

    bg = (HBRUSH)(COLOR_WINDOW+1);
    FillRect(dis->hDC, &dis->rcItem, bg);

    r = dis->rcItem;
    InflateRect(&r, -2, -2);

    gtkish_colors(disabled, pressed, &fill, &border, &fnt);
    draw_round_rect(dis->hDC, r, fill, border, 6);

    if (focused && !disabled){
        RECT fr = r;
        InflateRect(&fr, -3, -3);
        DrawFocusRect(dis->hDC, &fr);
    }

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fnt);
    if (g_uiFont) SelectObject(dis->hDC, g_uiFont);

    if (pressed){ r.left+=1; r.top+=1; r.right+=1; r.bottom+=1; }
    DrawTextW(dis->hDC, text, -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

/* -------------------------------------------------------------------------
   Owner-drawn status labels
   -------------------------------------------------------------------------
   The Moves label is aligned to the left side of the top row.
   The Time label is aligned to the right side of the top row.
------------------------------------------------------------------------- */
/* Draw the Moves or Time label from the current game state. */
static void draw_info_label(DRAWITEMSTRUCT* dis){
    HBRUSH bg;
    RECT r;
    UINT fmt;
    wchar_t buf[64];
    HFONT of;

    /* fill background to avoid ghosting */
    bg = GetSysColorBrush(COLOR_WINDOW);
    FillRect(dis->hDC, &dis->rcItem, bg);

    r = dis->rcItem;
    InflateRect(&r, -2, -2);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, RGB(0,0,0));
    of = NULL;
    if (g_uiFont) of = (HFONT)SelectObject(dis->hDC, g_uiFont);

    if (dis->CtlID == ID_LBL_MOVES){
        wsprintf(buf, L"Moves: %u", g_moves);
        fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
    }else{
        int t=g_elapsed, h=t/3600, m=(t%3600)/60, s=t%60;
        wsprintf(buf, L"Time: %02d:%02d:%02d", h,m,s);
        fmt = DT_RIGHT | DT_VCENTER | DT_SINGLELINE;
    }

    DrawTextW(dis->hDC, buf, -1, &r, fmt);

    if (of) SelectObject(dis->hDC, of);
}

/* -------------------------------------------------------------------------
   UI utility functions
   -------------------------------------------------------------------------
   These functions synchronize the internal board state with the visible button
   controls. The empty tile is shown as a disabled blank button.
------------------------------------------------------------------------- */
/* Enable or disable all numbered tiles. The blank cell is always disabled. */
static void enable_tiles(BOOL en){
    int i;
    for(i=0;i<TILE_COUNT;i++){
        if (g_pos[i]==0) { EnableWindow(g_tiles[i], FALSE); }
        else             { EnableWindow(g_tiles[i], en);    }
    }
}
/* Update button captions and enabled states so the UI matches g_pos[]. */
static void apply_board_to_buttons(void){
    int i;
    for(i=0;i<TILE_COUNT;i++){
        if (g_pos[i]==0){
            SetWindowTextW(g_tiles[i], L"");
            EnableWindow(g_tiles[i], FALSE);
        }else{
            wchar_t t[8]; wsprintf(t, L"%d", g_pos[i]);
            SetWindowTextW(g_tiles[i], t);
            if (!g_paused) EnableWindow(g_tiles[i], TRUE);
        }
        ShowWindow(g_tiles[i], SW_SHOWNA);
    }
    set_moves_label();
    set_time_label();
}
/* Older helper kept for the same purpose as set_menu_pause_label(). */
static void update_pause_menu_label(void){
    if (g_hFileMenu){
        ModifyMenuW(g_hFileMenu, IDM_FILE_PAUSE_RES, MF_BYCOMMAND|MF_STRING, IDM_FILE_PAUSE_RES,
                    g_paused ? L"Resume" : L"Pause");
        DrawMenuBar(g_hwnd);
    }
}

/* -------------------------------------------------------------------------
   Game commands
   -------------------------------------------------------------------------
   These functions implement menu commands such as New, Restart, Pause, Save,
   Load, Undo, Redo, and About.

   The code separates command behavior from the window procedure so the message
   handler stays readable.
------------------------------------------------------------------------- */
/* Reset the board using the current seed and start a fresh timer/history chain. */
static void reset_game(void){
    int i;
    g_moves=0; g_paused=FALSE; g_solved=FALSE; g_elapsed=0;
    new_solved(g_pos);
    srand(g_seed);
    shuffle_positions(g_pos);
    for(i=0;i<TILE_COUNT;i++) if(g_pos[i]==0){ g_blank=i; break; }

    for(i=0;i<TILE_COUNT;i++) g_start_pos[i] = g_pos[i];
    g_start_blank = g_blank;

    apply_board_to_buttons();
    enable_tiles(TRUE);

    g_start_time = time(NULL);
    if (!g_timer) g_timer = SetTimer(g_hwnd, IDT_ONESEC, 1000, NULL);
    update_pause_menu_label();
    history_reset();
}
/* Create a new random seed, then reset the game. */
static void do_new_game(void){
    g_seed = (unsigned int)time(NULL) ^ GetTickCount();
    reset_game();
}
/* Restart the current puzzle from the saved starting layout/seed. */
static void do_restart_game(void){
    reset_game();
}
/* Toggle paused state. While paused, the timer stops and tiles are disabled. */
static void do_pause_resume(void){
    g_paused = !g_paused;
    if (g_paused){
        if (g_timer){ KillTimer(g_hwnd, IDT_ONESEC); g_timer=0; }
        enable_tiles(FALSE);
    }else{
        g_start_time = time(NULL) - g_elapsed;
        if (!g_timer) g_timer = SetTimer(g_hwnd, IDT_ONESEC, 1000, NULL);
        enable_tiles(TRUE);
    }
    update_pause_menu_label();
}
/* Save the current board, starting board, timer, move count, and flags to disk. */
static void do_save_game(BOOL save_as){
    wchar_t path[MAX_PATH];
    HANDLE hf;
    DWORD written;
    GameSaveData save;

    if (save_as) { if (!choose_save_path(path, MAX_PATH)) return; }
    else get_default_save_path(path, MAX_PATH);

    ZeroMemory(&save, sizeof(save));
    save.signature = SAVE_SIGNATURE;
    save.version = SAVE_VERSION;
    pack_board(g_pos, save.pos_packed);
    pack_board(g_start_pos, save.start_packed);
    save.seed = g_seed;
    save.moves = g_moves;
    save.elapsed = (DWORD)g_elapsed;
    save.flags = (BYTE)((g_paused ? 1 : 0) | (g_solved ? 2 : 0));

    hf = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE){
        MessageBoxW(g_hwnd, L"Could not create save file.", APP_TITLE, MB_OK|MB_ICONERROR);
        return;
    }

    if (!WriteFile(hf, &save, sizeof(save), &written, NULL) || written != sizeof(save)){
        CloseHandle(hf);
        MessageBoxW(g_hwnd, L"Could not write save file.", APP_TITLE, MB_OK|MB_ICONERROR);
        return;
    }
    CloseHandle(hf);
}
/* Load a saved game, validate it, restore it, and reset undo/redo history. */
static void do_load_game(BOOL from_file){
    wchar_t path[MAX_PATH];
    HANDLE hf;
    DWORD readBytes;
    GameSaveData save;

    if (from_file) { if (!choose_load_path(path, MAX_PATH)) return; }
    else get_default_save_path(path, MAX_PATH);

    ZeroMemory(&save, sizeof(save));
    hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE){
        MessageBoxW(g_hwnd, L"Could not open save file.", APP_TITLE, MB_OK|MB_ICONERROR);
        return;
    }

    if (!ReadFile(hf, &save, sizeof(save), &readBytes, NULL) || readBytes != sizeof(save)){
        CloseHandle(hf);
        MessageBoxW(g_hwnd, L"Could not read save file.", APP_TITLE, MB_OK|MB_ICONERROR);
        return;
    }
    CloseHandle(hf);

    if (save.signature != SAVE_SIGNATURE || save.version != SAVE_VERSION){
        MessageBoxW(g_hwnd, L"Invalid save file format.", APP_TITLE, MB_OK|MB_ICONERROR);
        return;
    }
    {
        int pos[TILE_COUNT], start_pos[TILE_COUNT], i;
        GameState state;
        unpack_board(save.pos_packed, pos);
        unpack_board(save.start_packed, start_pos);
        if (!is_valid_board(pos) || !is_valid_board(start_pos) ||
            !is_solvable(pos) || !is_solvable(start_pos) || is_solved_positions(start_pos)){
            MessageBoxW(g_hwnd, L"Save file contents are invalid.", APP_TITLE, MB_OK|MB_ICONERROR);
            return;
        }
        for (i = 0; i < TILE_COUNT; i++){ g_start_pos[i] = start_pos[i]; state.pos[i] = pos[i]; }
        g_seed = save.seed;
        g_start_blank = 0;
        g_blank = 0;
        for (i = 0; i < TILE_COUNT; i++){
            if (g_start_pos[i] == 0) g_start_blank = i;
            if (state.pos[i] == 0) g_blank = i;
        }
        state.blank = g_blank; state.moves = save.moves; state.elapsed = (int)save.elapsed;
        state.paused = (save.flags & 1) != 0; state.solved = (save.flags & 2) != 0;
        set_game_state_from_state(&state);
        history_reset();
    }
}
/* Move one step backward in the history list, if possible. */
static void do_undo(void){
    if (g_history_index > 0){ g_history_index--; set_game_state_from_state(&g_history[g_history_index]); }
}
/* Move one step forward in the history list, if possible. */
static void do_redo(void){
    if (g_history_index + 1 < g_history_count){ g_history_index++; set_game_state_from_state(&g_history[g_history_index]); }
}
/* Show a simple About dialog. */
static void do_about(void){
    MessageBoxW(g_hwnd, L"Fifteen Puzzle Game\n\nDeveloped using Win32 API.", APP_TITLE, MB_OK|MB_ICONINFORMATION);
}

/* -------------------------------------------------------------------------
   Player interaction
   -------------------------------------------------------------------------
   A click is valid when the selected tile is in the same row or column as the
   blank space. Unlike a very strict implementation that moves only adjacent
   tiles, this version allows a whole row or column segment to slide into the
   empty space. That makes the game smoother and more convenient to play.
------------------------------------------------------------------------- */
/* Swap two board cells and immediately update the corresponding button captions. */
static void swap_idx(int a, int b){
    int t;
    HWND hb1, hb2;
    wchar_t s1[8], s2[8];

    t=g_pos[a]; g_pos[a]=g_pos[b]; g_pos[b]=t;
    if (g_pos[a]==0) g_blank=a; else if (g_pos[b]==0) g_blank=b;

    hb1=g_tiles[a]; hb2=g_tiles[b];

    if (g_pos[a]==0){ SetWindowTextW(hb1,L""); EnableWindow(hb1,FALSE); }
    else { wsprintf(s1,L"%d",g_pos[a]); SetWindowTextW(hb1,s1); if(!g_paused) EnableWindow(hb1,TRUE); }

    if (g_pos[b]==0){ SetWindowTextW(hb2,L""); EnableWindow(hb2,FALSE); }
    else { wsprintf(s2,L"%d",g_pos[b]); SetWindowTextW(hb2,s2); if(!g_paused) EnableWindow(hb2,TRUE); }
}
/* Handle a tile click, slide tiles if the move is legal, then check for solved state. */
static void try_click_index(int idx){
    int br=g_blank/GRID_SIZE, bc=g_blank%GRID_SIZE;
    int r=idx/GRID_SIZE, c=idx%GRID_SIZE;
    int i;

    if (g_paused || g_solved) return;
    if (idx<0 || idx>=TILE_COUNT) return;
    if (g_pos[idx]==0) return;

    if (r==br && c!=bc){
        if (c<bc){ for(i=bc; i>c; i--) swap_idx(r*GRID_SIZE+i, r*GRID_SIZE+i-1); }
        else     { for(i=bc; i<c; i++) swap_idx(r*GRID_SIZE+i, r*GRID_SIZE+i+1); }
        g_moves++;
    }else if (c==bc && r!=br){
        if (r<br){ for(i=br; i>r; i--) swap_idx(i*GRID_SIZE+c, (i-1)*GRID_SIZE+c); }
        else     { for(i=br; i<r; i++) swap_idx(i*GRID_SIZE+c, (i+1)*GRID_SIZE+c); }
        g_moves++;
    }else{
        return;
    }
    set_moves_label();

    if (is_solved_positions(g_pos)){
        int h=g_elapsed/3600, m=(g_elapsed%3600)/60, s=g_elapsed%60;
        wchar_t msg[128];
        g_solved=TRUE;
        wsprintf(msg, L"Congratulations! You solved the puzzle!\n\nTotal Moves: %u\nTotal Time: %02d:%02d:%02d",
                 g_moves, h,m,s);
        MessageBoxW(g_hwnd, msg, APP_TITLE, MB_OK|MB_ICONINFORMATION);
    }
    history_record();
}

/* -------------------------------------------------------------------------
   Layout
   -------------------------------------------------------------------------
   The UI is calculated manually using MoveWindow(). This is typical for small
   Win32 API samples and avoids the need for resource-dialog layout or a GUI
   framework.

   When the main window is resized, WM_SIZE calls layout_children() and the board
   is recalculated to fill the available client area.
------------------------------------------------------------------------- */
/* Place the status labels and 4x4 tile grid inside the current client rectangle. */
static void layout_children(void){
    RECT rc;
    int w,h;
    int gridW, gridH, innerW, innerH, border=PADDING, spacing=PADDING;
    int i;

    GetClientRect(g_hwnd, &rc);
    w = rc.right - rc.left; h = rc.bottom - rc.top;

    /* owner-drawn labels (positions only; text drawn in WM_DRAWITEM) */
    MoveWindow(g_lblMoves, 10, 8, 120, TOP_ROW_H-14, TRUE);
    MoveWindow(g_lblTime,  w-170, 8, 160, TOP_ROW_H-14, TRUE);

    g_gridRect.left = 0;
    g_gridRect.top  = TOP_ROW_H;
    g_gridRect.right= w;
    g_gridRect.bottom=h;

    gridW = g_gridRect.right - g_gridRect.left;
    gridH = g_gridRect.bottom - g_gridRect.top;

    innerW = gridW - 2*border;
    innerH = gridH - 2*border;
    if (innerW < 0) innerW=0; if(innerH<0) innerH=0;

    if (GRID_SIZE>0){
        g_cellW = (innerW - (GRID_SIZE-1)*spacing)/GRID_SIZE;
        g_cellH = (innerH - (GRID_SIZE-1)*spacing)/GRID_SIZE;
        if (g_cellW<1) g_cellW=1;
        if (g_cellH<1) g_cellH=1;
    }else{
        g_cellW=g_cellH=1;
    }

    for(i=0;i<TILE_COUNT;i++){
        int rr=i/GRID_SIZE, cc=i%GRID_SIZE;
        int x = g_gridRect.left + border + cc*(g_cellW+spacing);
        int y = g_gridRect.top  + border + rr*(g_cellH+spacing);
        MoveWindow(g_tiles[i], x, y, g_cellW, g_cellH, TRUE);
    }
}

/* -------------------------------------------------------------------------
   Window procedure
   -------------------------------------------------------------------------
   This is the central message handler of the application. Windows sends messages
   here for creation, resizing, menu commands, timers, drawing, and destruction.
------------------------------------------------------------------------- */
/* Main callback that reacts to Win32 messages. */
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l){
    switch(m){
    case WM_CREATE:{
        int i;
        HDC hdc;
        int dpiY;
        LOGFONTW lf;

        g_hwnd = h;

        /* Create the top-level menu entirely in code.
           File contains game/session commands.
           Edit contains Undo/Redo.
           Help contains the About dialog. */
        g_hMenuMain = CreateMenu();
        g_hFileMenu = CreatePopupMenu();
        AppendMenuW(g_hFileMenu, MF_STRING, IDM_FILE_NEW,       L"New");
        AppendMenuW(g_hFileMenu, MF_STRING, IDM_FILE_RESTART,   L"Restart");
        AppendMenuW(g_hFileMenu, MF_STRING, IDM_FILE_PAUSE_RES, L"Pause");
        AppendMenuW(g_hFileMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(g_hFileMenu, MF_STRING, IDM_FILE_SAVE,      L"Save\tCtrl+S");
        AppendMenuW(g_hFileMenu, MF_STRING, IDM_FILE_LOAD,      L"Load\tCtrl+O");
        AppendMenuW(g_hFileMenu, MF_STRING, IDM_FILE_SAVE + 10, L"Save As...\tCtrl+Shift+S");
        AppendMenuW(g_hFileMenu, MF_STRING, IDM_FILE_LOAD + 10, L"Load From...\tCtrl+Shift+O");
        AppendMenuW(g_hFileMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(g_hFileMenu, MF_STRING, IDM_FILE_EXIT,      L"Exit");
        AppendMenuW(g_hMenuMain, MF_POPUP, (UINT_PTR)g_hFileMenu, L"File");
        {
            HMENU hEdit = CreatePopupMenu();
            AppendMenuW(hEdit, MF_STRING, IDM_EDIT_UNDO, L"Undo\tCtrl+Z");
            AppendMenuW(hEdit, MF_STRING, IDM_EDIT_REDO, L"Redo\tCtrl+Y");
            AppendMenuW(g_hMenuMain, MF_POPUP, (UINT_PTR)hEdit, L"Edit");
        }
        {
            HMENU hHelp = CreatePopupMenu();
            AppendMenuW(hHelp, MF_STRING, IDM_HELP_ABOUT, L"About");
            AppendMenuW(g_hMenuMain, MF_POPUP, (UINT_PTR)hHelp, L"Help");
        }
        SetMenu(h, g_hMenuMain);
        {
            ACCEL keys[6];
            keys[0].fVirt = FCONTROL|FVIRTKEY; keys[0].key = 'S'; keys[0].cmd = IDM_FILE_SAVE;
            keys[1].fVirt = FCONTROL|FSHIFT|FVIRTKEY; keys[1].key = 'S'; keys[1].cmd = IDM_FILE_SAVE + 10;
            keys[2].fVirt = FCONTROL|FVIRTKEY; keys[2].key = 'O'; keys[2].cmd = IDM_FILE_LOAD;
            keys[3].fVirt = FCONTROL|FSHIFT|FVIRTKEY; keys[3].key = 'O'; keys[3].cmd = IDM_FILE_LOAD + 10;
            keys[4].fVirt = FCONTROL|FVIRTKEY; keys[4].key = 'Z'; keys[4].cmd = IDM_EDIT_UNDO;
            keys[5].fVirt = FCONTROL|FVIRTKEY; keys[5].key = 'Y'; keys[5].cmd = IDM_EDIT_REDO;
            g_accel = CreateAcceleratorTableW(keys, 6);
        }

        /* Create a Segoe UI font at roughly 10 points.
           This makes the controls look closer to a modern Windows application
           than the old default dialog font. */
        hdc = GetDC(h);
        dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(h, hdc);

        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -MulDiv(10, dpiY, 72); /* ~10pt */
        lstrcpynW(lf.lfFaceName, L"Segoe UI", LF_FACESIZE);
        g_uiFont = CreateFontIndirectW(&lf);

        /* Owner-drawn labels */
        g_lblMoves = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_VISIBLE|SS_OWNERDRAW,
                                   10,8,120,20, h, (HMENU)ID_LBL_MOVES, NULL, NULL);
        g_lblTime  = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_VISIBLE|SS_OWNERDRAW,
                                   200,8,160,20, h, (HMENU)ID_LBL_TIME, NULL, NULL);
        SendMessageW(g_lblMoves, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        SendMessageW(g_lblTime,  WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        /* Create the 16 tile buttons. Their exact positions are calculated later
           by layout_children(), so they are initially created at 0,0 with 0x0 size. */
        for(i=0;i<TILE_COUNT;i++){
            g_tiles[i] = CreateWindowW(L"BUTTON", L"",
                            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON|BS_OWNERDRAW,
                            0,0,0,0, h, (HMENU)(ID_TILE_BASE+i), NULL, NULL);
            SendMessageW(g_tiles[i], WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        }

        do_new_game();
        return 0;
    }

    case WM_SIZE:
        layout_children();
        return 0;

    case WM_COMMAND:{
        WORD id = LOWORD(w);
        if (id == IDM_FILE_NEW)       { do_new_game();      return 0; }
        if (id == IDM_FILE_RESTART)   { do_restart_game();  return 0; }
        if (id == IDM_FILE_PAUSE_RES) { do_pause_resume();  return 0; }
        if (id == IDM_FILE_SAVE)      { do_save_game(FALSE);return 0; }
        if (id == IDM_FILE_SAVE + 10) { do_save_game(TRUE); return 0; }
        if (id == IDM_FILE_LOAD)      { do_load_game(FALSE); return 0; }
        if (id == IDM_FILE_LOAD + 10) { do_load_game(TRUE); return 0; }
        if (id == IDM_EDIT_UNDO)      { do_undo();          return 0; }
        if (id == IDM_EDIT_REDO)      { do_redo();          return 0; }
        if (id == IDM_FILE_EXIT)      { PostMessage(h, WM_CLOSE, 0, 0); return 0; }
        if (id == IDM_HELP_ABOUT)     { do_about();         return 0; }

        if (id >= ID_TILE_BASE && id < ID_TILE_BASE + TILE_COUNT){
            try_click_index(id - ID_TILE_BASE);
            return 0;
        }
        return 0;
    }

    case WM_TIMER:
        if (w == IDT_ONESEC){
            if (!g_solved && !g_paused){
                time_t now = time(NULL);
                g_elapsed = (int)(now - g_start_time);
                set_time_label();
            }
            return 0;
        }
        break;

    case WM_DRAWITEM:{
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)l;
        if (dis->CtlType == ODT_BUTTON){
            draw_tile_button(dis);
            return TRUE;
        }
        if (dis->CtlType == ODT_STATIC){
            draw_info_label(dis);
            return TRUE;
        }
        break;
    }

    case WM_GETMINMAXINFO:{
        MINMAXINFO* mi=(MINMAXINFO*)l;
        mi->ptMinTrackSize.x = 260;
        mi->ptMinTrackSize.y = 300;
        return 0;
    }

    case WM_DESTROY:
        if (g_timer){ KillTimer(h, IDT_ONESEC); g_timer=0; }
        if (g_accel){ DestroyAcceleratorTable(g_accel); g_accel=NULL; }
        if (g_uiFont){ DeleteObject(g_uiFont); g_uiFont=NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

/* -------------------------------------------------------------------------
   Program entry point
   -------------------------------------------------------------------------
   wWinMain registers the window class, creates the main window, shows it, and
   runs the classic Win32 message loop.

   TranslateAccelerator() is used before TranslateMessage()/DispatchMessage() so
   keyboard shortcuts such as Ctrl+S, Ctrl+O, Ctrl+Z, and Ctrl+Y work.
------------------------------------------------------------------------- */
/* Application entry point for a Unicode Win32 GUI program. */
int APIENTRY wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show){
    WNDCLASSW wc;
    HWND wnd;
    MSG msg;

	ZeroMemory(&wc, sizeof(wc));
	wc.style         = CS_HREDRAW|CS_VREDRAW;
	wc.lpfnWndProc   = WndProc;
	wc.hInstance     = hi;
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wc.lpszClassName = APP_CLASS;
	wc.hIcon   = LoadIcon(hi, MAKEINTRESOURCE(IDI_ICON1));

    if (!RegisterClassW(&wc)) return 0;

    wnd = CreateWindowW(APP_CLASS, APP_TITLE,
                        WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, CW_USEDEFAULT, 280, 360,
                        NULL, NULL, hi, NULL);
    if (!wnd) return 0;

    ShowWindow(wnd, show);
    UpdateWindow(wnd);

    while (GetMessage(&msg,NULL,0,0)){
        if (!TranslateAccelerator(wnd, g_accel, &msg)){
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
    }
    return (int)msg.wParam;
}
