#include "common.h"
#include "terminal.h"
#include "terminal_internal.h"

void disableRawMode(void) {
    editorRestoreWindowMinSize();

    const char *sequence = "\x1b[?1049l";
    DWORD written;
    WriteConsoleA(E.hout, sequence, (DWORD)strlen(sequence), &written, NULL);
    SetConsoleMode(E.hin, E.original_in_mode);
    SetConsoleMode(E.hout, E.original_out_mode);
    SetConsoleTextAttribute(E.hout, 7);
}

void enableRawMode(void) {
    E.hin = GetStdHandle(STD_INPUT_HANDLE);
    E.hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (E.hin == INVALID_HANDLE_VALUE || E.hout == INVALID_HANDLE_VALUE)
        die("GetStdHandle");
    if (!GetConsoleMode(E.hin, &E.original_in_mode))
        die("GetConsoleMode input");
    if (!GetConsoleMode(E.hout, &E.original_out_mode))
        die("GetConsoleMode output");

    DWORD input_mode = E.original_in_mode;
    input_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                    ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
    input_mode |= ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
    if (!SetConsoleMode(E.hin, input_mode)) die("SetConsoleMode input");

    DWORD output_mode = E.original_out_mode;
    output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                   DISABLE_NEWLINE_AUTO_RETURN;
    SetConsoleMode(E.hout, output_mode);

    const char *sequence = "\x1b[?1049h\x1b[H\x1b[2J";
    DWORD written;
    WriteConsoleA(E.hout, sequence, (DWORD)strlen(sequence), &written, NULL);
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    editorInstallWindowMinSize();
    hideConsoleScrollbars();
    atexit(disableRawMode);
}
