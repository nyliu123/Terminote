#include "common.h"
#include "terminal.h"
#include "row.h"
#include "editor.h"
#include "output.h"
#include "keybindings.h"
#include "colors.h"
#include "completion.h"
#include "bracket.h"
#include "tabs.h"
#include "settings.h"
#include "localization.h"

static int termWidth(const char *text) {
    return utf8_terminal_width(text);
}

static void appendSpaces(int count) {
    while (count-- > 0) abAppend(" ");
}

static int appendClippedTailText(const char *text, int max_width) {
    int width = termWidth(text);
    if (width <= max_width) {
        abAppend(text);
        return width;
    }
    if (max_width <= 0) return 0;
    const char *tail = text;
    int target = max_width > 2 ? max_width - 2 : max_width;
    while (*tail && termWidth(tail) > target) {
        int length;
        utf8_terminal_char_width(tail, &length);
        tail += length;
    }
    if (max_width > 2) abAppend("..");
    abAppend(tail);
    return max_width;
}

static int editorInputActivityIsCurrent(void) {
    return E.input_activity_deadline > 0 &&
           GetTickCount64() < E.input_activity_deadline;
}

static void selectionEventText(char *buffer, size_t size) {
    int sr = E.sel_anchor_row, sc = E.sel_anchor_col;
    int er = E.cy, ec = E.cx;
    if (er < sr || (er == sr && ec < sc)) {
        int swap = sr; sr = er; er = swap;
        swap = sc; sc = ec; ec = swap;
    }
    if (sr >= E.numrows) sr = E.numrows - 1;
    if (er >= E.numrows) er = E.numrows - 1;
    if (sc > E.row[sr].size) sc = E.row[sr].size;
    if (ec > E.row[er].size) ec = E.row[er].size;

    int chars = ec - sc;
    if (sr != er) {
        chars = E.row[sr].size - sc + ec + er - sr;
        for (int row = sr + 1; row < er; row++) chars += E.row[row].size;
    }
    int lines = er - sr + 1;
    int all = sr == 0 && sc == 0 && er == E.numrows - 1 &&
              ec == E.row[er].size;
    if (all) snprintf(buffer, size, "%s", L("selection.all"));
    else if (lines > 1)
        snprintf(buffer, size, L("selection.lines"), lines, chars);
    else snprintf(buffer, size, L("selection.characters"), chars);
}

static const char *eventBarText(char *buffer, int input_active,
                                int has_selection) {
    if (editorSettingsOpen()) return L("settings.event");
    if ((E.prompt_active || E.search_active) && E.statusmsg[0])
        return E.statusmsg;
    if (E.mouse_drag_move) return L("editor.content_drag");
    if (E.hscroll_dragging && E.cur_event[0]) return E.cur_event;
    if (!input_active && !has_selection) return L("editor.ready");
    if (input_active && E.statusmsg[0]) return E.statusmsg;
    if (input_active && E.cur_event[0]) return E.cur_event;
    if (!has_selection) return L("editor.ready");
    selectionEventText(buffer, 256);
    return buffer;
}

void editorDrawStatusBar(void) {
    long total_chars = 0, position = 0;
    for (int i = 0; i < E.numrows; i++) {
        total_chars += E.row[i].size;
        if (i < E.cy) position += E.row[i].size + 1;
    }
    position += E.cx;

    char stats[120] = "";
    if (E.numrows > 0)
        snprintf(stats, sizeof(stats),
                 L("status.statistics"),
                 total_chars, E.numrows, E.cy + 1, E.cx, position);

    const char *button = editorSettingsButtonLabel();
    int button_width = editorSettingsButtonWidth();
    int stats_width = termWidth(stats);
    if (button_width > E.screencols) button_width = E.screencols;
    if (stats_width > E.screencols - button_width)
        stats_width = E.screencols - button_width;

    abAppend(editorColor(COLOR_RESET));
    if (editorSettingsOpen()) {
        abAppend(editorColor(COLOR_SETTINGS_MENU_BACKGROUND));
        abAppend(editorColor(COLOR_SETTINGS_MENU_TEXT));
    } else if (editorSettingsButtonHovered()) {
        abAppend(editorColor(COLOR_SETTINGS_BUTTON_HOVER_BACKGROUND));
        abAppend(editorColor(COLOR_SETTINGS_BUTTON_HOVER_TEXT));
    } else {
        abAppend(editorColor(COLOR_STATUS_BAR));
    }
    int button_drawn = termWidth(button);
    if (button_drawn <= button_width) {
        abAppend(button);
        appendSpaces(button_width - button_drawn);
    } else {
        appendClippedTailText(button, button_width);
    }
    abAppend(editorColor(COLOR_RESET));
    abAppend(editorColor(COLOR_STATUS_BAR));
    appendSpaces(E.screencols - button_width - stats_width);
    if (stats_width == termWidth(stats)) abAppend(stats);
    else appendClippedTailText(stats, stats_width);
    abAppend(editorColor(COLOR_RESET));
}

void editorDrawEventBar(void) {
    abAppend(editorColor(COLOR_EVENT_BAR));
    abAppend("\x1b[K");
    char buffer[256];
    int input_active = editorInputActivityIsCurrent();
    int has_selection = E.sel_active && E.numrows > 0;
    abAppend(eventBarText(buffer, input_active, has_selection));
    abAppend(editorColor(COLOR_RESET));
    abAppend("\r\n");
}

void editorRefreshScreen(void) {
    int old_cols = E.screencols;
    int old_rows = E.screenrows;
    editorFitBufferToWindow();
    if (E.screencols != old_cols || E.screenrows != old_rows) {
        editorUpdateLineNumWidth();
        hideConsoleScrollbars();
    }

    editorScroll();
    editorUpdateBracketMatch();
    abAppend("\x1b[?25l");
    abAppend("\x1b[H");
    editorDrawTabBar();
    editorDrawRows();
    editorDrawEventBar();
    editorDrawStatusBar();
    editorDrawCompletionPopup();
    editorDrawTabDropdown();
    editorDrawSettingsPopup();

    if (!editorTabDropdownOpen() && !editorSettingsOpen()) {
        int screen_y = E.cy - E.rowoff + 2;
        int cursor_x = E.rx - E.coloff + E.linenum_width + 2;
        if (cursor_x < E.linenum_width + 2)
            cursor_x = E.linenum_width + 2;
        if (cursor_x > E.screencols) cursor_x = E.screencols;
        if (screen_y >= 2 && screen_y < editorVisibleRows() + 2) {
            char sequence[32];
            snprintf(sequence, sizeof(sequence), "\x1b[%d;%dH",
                     screen_y, cursor_x);
            abAppend(sequence);
            abAppend("\x1b[?25h");
        }
    }

    abWrite();
    abFree();
    hideConsoleScrollbars();
}

void editorSetEvent(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(E.cur_event, sizeof(E.cur_event), format, arguments);
    va_end(arguments);
}

void editorSetStatusMessage(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), format, arguments);
    va_end(arguments);
    E.statusmsg_dirty = 1;
}
