#include "common.h"
#include "terminal.h"
#include "terminal_internal.h"
#include "output.h"

static HWND min_size_hwnd = NULL;
static WNDPROC original_console_wndproc = NULL;

static void editorStoreWindowSize(int width, int height) {
    E.screencols = width;
    E.screenrows = height - 3;
    if (E.screenrows < 1) E.screenrows = 1;
}

static int editorReadWindowSize(int *width, int *height) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(E.hout, &csbi)) return 0;
    *width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 1;
}

static void editorRequestTerminalResize(int rows, int cols) {
    char sequence[32];
    int length = snprintf(sequence, sizeof(sequence),
                          "\x1b[8;%d;%dt", rows, cols);
    DWORD written;
    if (length > 0 && length < (int)sizeof(sequence))
        WriteConsoleA(E.hout, sequence, (DWORD)length, &written, NULL);
}

static int editorWindowPixelsForCells(HWND hwnd, int rows, int cols,
                                      int *width, int *height) {
    CONSOLE_FONT_INFOEX font;
    RECT rectangle = {0, 0, 0, 0};
    if (!hwnd) return 0;

    memset(&font, 0, sizeof(font));
    font.cbSize = sizeof(font);
    if (!GetCurrentConsoleFontEx(E.hout, FALSE, &font) ||
        font.dwFontSize.X < 1 || font.dwFontSize.Y < 1)
        return 0;

    rectangle.right = cols * font.dwFontSize.X;
    rectangle.bottom = rows * font.dwFontSize.Y;
    DWORD style = (DWORD)GetWindowLongPtr(hwnd, GWL_STYLE);
    DWORD ex_style = (DWORD)GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (!AdjustWindowRectEx(&rectangle, style, FALSE, ex_style)) return 0;
    *width = rectangle.right - rectangle.left;
    *height = rectangle.bottom - rectangle.top;
    return 1;
}

static void editorClampSizingRect(HWND hwnd, WPARAM edge, RECT *rectangle) {
    int min_rows, min_cols, min_width, min_height;
    editorHelpMinimumSize(&min_rows, &min_cols);
    if (!editorWindowPixelsForCells(hwnd, min_rows, min_cols,
                                    &min_width, &min_height))
        return;

    if (rectangle->right - rectangle->left < min_width) {
        if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT ||
            edge == WMSZ_BOTTOMLEFT)
            rectangle->left = rectangle->right - min_width;
        else
            rectangle->right = rectangle->left + min_width;
    }
    if (rectangle->bottom - rectangle->top < min_height) {
        if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT ||
            edge == WMSZ_TOPRIGHT)
            rectangle->top = rectangle->bottom - min_height;
        else
            rectangle->bottom = rectangle->top + min_height;
    }
}

static void editorResizeWindowPixels(int rows, int cols) {
    HWND hwnd = GetConsoleWindow();
    int width, height;
    if (!editorWindowPixelsForCells(hwnd, rows, cols, &width, &height)) return;
    SetWindowPos(hwnd, NULL, 0, 0, width, height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK editorMinSizeWndProc(HWND hwnd, UINT message,
                                              WPARAM w_param, LPARAM l_param) {
    if (message == WM_SIZING) {
        editorClampSizingRect(hwnd, w_param, (RECT *)l_param);
        return TRUE;
    }

    LRESULT result = original_console_wndproc
        ? CallWindowProc(original_console_wndproc, hwnd, message, w_param, l_param)
        : DefWindowProc(hwnd, message, w_param, l_param);

    if (message == WM_GETMINMAXINFO) {
        int min_rows, min_cols, min_width, min_height;
        editorHelpMinimumSize(&min_rows, &min_cols);
        if (editorWindowPixelsForCells(hwnd, min_rows, min_cols,
                                      &min_width, &min_height)) {
            MINMAXINFO *info = (MINMAXINFO *)l_param;
            if (info->ptMinTrackSize.x < min_width)
                info->ptMinTrackSize.x = min_width;
            if (info->ptMinTrackSize.y < min_height)
                info->ptMinTrackSize.y = min_height;
        }
    }
    return result;
}

void editorInstallWindowMinSize(void) {
    if (original_console_wndproc) return;
    HWND hwnd = GetConsoleWindow();
    if (!hwnd) return;

    SetLastError(0);
    LONG_PTR previous = SetWindowLongPtr(hwnd, GWLP_WNDPROC,
                                         (LONG_PTR)editorMinSizeWndProc);
    if (previous == 0 && GetLastError() != 0) return;
    original_console_wndproc = (WNDPROC)previous;
    min_size_hwnd = hwnd;
}

void editorRestoreWindowMinSize(void) {
    if (!original_console_wndproc || !min_size_hwnd) return;
    SetWindowLongPtr(min_size_hwnd, GWLP_WNDPROC,
                     (LONG_PTR)original_console_wndproc);
    original_console_wndproc = NULL;
    min_size_hwnd = NULL;
}

void editorFitBufferToWindow(void) {
    int min_width, min_height, width, height;
    editorHelpMinimumSize(&min_height, &min_width);
    if (!editorReadWindowSize(&width, &height)) return;

    for (int attempt = 0; attempt < 3; attempt++) {
        int target_width = width < min_width ? min_width : width;
        int target_height = height < min_height ? min_height : height;
        if (target_width == width && target_height == height) break;
        if (target_width < 1 || target_height < 1) break;

        editorResizeWindowPixels(target_height, target_width);
        editorRequestTerminalResize(target_height, target_width);

        SMALL_RECT parked = {0, 0, (SHORT)(width - 1), (SHORT)(height - 1)};
        SetConsoleWindowInfo(E.hout, TRUE, &parked);
        COORD size = {(SHORT)target_width, (SHORT)target_height};
        SetConsoleScreenBufferSize(E.hout, size);
        SMALL_RECT window = {0, 0, (SHORT)(target_width - 1),
                             (SHORT)(target_height - 1)};
        SetConsoleWindowInfo(E.hout, TRUE, &window);
        SetConsoleScreenBufferSize(E.hout, size);
        Sleep(10);

        if (!editorReadWindowSize(&width, &height)) {
            width = target_width;
            height = target_height;
            break;
        }
    }

    editorStoreWindowSize(width, height);
    hideConsoleScrollbars();
}

void hideConsoleScrollbars(void) {
    HWND hwnd = GetConsoleWindow();
    if (!hwnd) return;
    ShowScrollBar(hwnd, SB_VERT, FALSE);

    SCROLLINFO info;
    info.cbSize = sizeof(SCROLLINFO);
    info.fMask = SIF_RANGE | SIF_PAGE;
    if (GetScrollInfo(hwnd, SB_HORZ, &info)) {
        int range = info.nMax - info.nMin - (int)info.nPage + 1;
        ShowScrollBar(hwnd, SB_HORZ, range < 1 ? FALSE : TRUE);
    }
}

int getWindowSize(int *rows, int *cols) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(E.hout, &csbi)) return -1;
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 0;
}
