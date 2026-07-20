#include "common.h"
#include "terminal.h"
#include "output.h"
#include "keybindings.h"
#include "colors.h"
#include "localization.h"

struct helpEntry {
    int action;
    const char *key;
    const char *desc;
};

static const struct helpEntry help[] = {
    {0, "help.section_file", NULL},
    {ACTION_SAVE, NULL, "help.save"},
    {ACTION_SAVE_AS, NULL, "help.save_as"},
    {ACTION_OPEN, NULL, "help.open"},
    {ACTION_NEW_FILE, NULL, "help.new"},
    {ACTION_NEXT_TAB, NULL, "help.next_tab"},
    {ACTION_RUN, NULL, "help.run"},
    {ACTION_SETTINGS, NULL, "help.settings"},
    {ACTION_QUIT_WITHOUT_SAVING, NULL, "help.quit_without_saving"},
    {0, "help.section_edit", NULL},
    {ACTION_FIND, NULL, "help.find"},
    {ACTION_FIND_AND_REPLACE, NULL, "help.replace"},
    {ACTION_UNDO, NULL, "help.undo"},
    {ACTION_REDO, NULL, "help.redo"},
    {ACTION_CUT, NULL, "help.cut"},
    {ACTION_COPY, NULL, "help.copy"},
    {ACTION_PASTE, NULL, "help.paste"},
    {ACTION_SELECT_ALL, NULL, "help.select_all"},
    {0, "help.section_cursor", NULL},
    {ACTION_MOVE_TO_LINE_START, NULL, "help.line_start"},
    {ACTION_MOVE_TO_LINE_END, NULL, "help.line_end"},
    {ACTION_PAGE_UP, NULL, "help.page_up"},
    {ACTION_PAGE_DOWN, NULL, "help.page_down"},
    {0, "help.section_mouse", NULL},
    {0, "help.mouse_wheel", "help.scroll"},
    {0, "help.mouse_left", "help.position_cursor"},
    {0, NULL, NULL}
};

static unsigned int helpCodepoint(const char *text, int *length) {
    const unsigned char *bytes = (const unsigned char *)text;
    int size = utf8_seq_len(bytes[0]);
    if (size == 1) {
        *length = 1;
        return bytes[0];
    }
    for (int i = 1; i < size; i++) {
        if (!bytes[i] || !is_utf8_cont(bytes[i])) {
            *length = 1;
            return bytes[0];
        }
    }

    unsigned int codepoint;
    if (size == 2)
        codepoint = ((unsigned int)(bytes[0] & 0x1f) << 6) |
                    (unsigned int)(bytes[1] & 0x3f);
    else if (size == 3)
        codepoint = ((unsigned int)(bytes[0] & 0x0f) << 12) |
                    ((unsigned int)(bytes[1] & 0x3f) << 6) |
                    (unsigned int)(bytes[2] & 0x3f);
    else
        codepoint = ((unsigned int)(bytes[0] & 0x07) << 18) |
                    ((unsigned int)(bytes[1] & 0x3f) << 12) |
                    ((unsigned int)(bytes[2] & 0x3f) << 6) |
                    (unsigned int)(bytes[3] & 0x3f);
    *length = size;
    return codepoint;
}

static int helpCodepointWidth(unsigned int codepoint) {
    if ((codepoint >= 0x0300 && codepoint <= 0x036f) ||
        (codepoint >= 0x1ab0 && codepoint <= 0x1aff) ||
        (codepoint >= 0x1dc0 && codepoint <= 0x1dff) ||
        (codepoint >= 0x20d0 && codepoint <= 0x20ff) ||
        (codepoint >= 0xfe20 && codepoint <= 0xfe2f))
        return 0;
    if ((codepoint >= 0x1100 && codepoint <= 0x115f) ||
        codepoint == 0x2329 || codepoint == 0x232a ||
        (codepoint >= 0x2e80 && codepoint <= 0xa4cf &&
         codepoint != 0x303f) ||
        (codepoint >= 0xac00 && codepoint <= 0xd7a3) ||
        (codepoint >= 0xf900 && codepoint <= 0xfaff) ||
        (codepoint >= 0xfe10 && codepoint <= 0xfe19) ||
        (codepoint >= 0xfe30 && codepoint <= 0xfe6f) ||
        (codepoint >= 0xff00 && codepoint <= 0xff60) ||
        (codepoint >= 0xffe0 && codepoint <= 0xffe6) ||
        (codepoint >= 0x1f300 && codepoint <= 0x1faff) ||
        (codepoint >= 0x20000 && codepoint <= 0x3fffd))
        return 2;
    return 1;
}

static int termWidth(const char *text) {
    int width = 0;
    while (*text) {
        int length;
        unsigned int codepoint = helpCodepoint(text, &length);
        width += helpCodepointWidth(codepoint);
        text += length;
    }
    return width;
}

static int hasHelpEntry(int index) {
    return help[index].action || help[index].key || help[index].desc;
}

static const char *helpEntryKey(int index) {
    if (help[index].action)
        return editorBindingLabel(help[index].action);
    return L(help[index].key);
}

static void helpColumnWidths(int *key_width, int *desc_width) {
    int longest_key = 0;
    int longest_desc = 0;
    for (int i = 0; hasHelpEntry(i); i++) {
        if (!help[i].desc) continue;
        int current_key = termWidth(helpEntryKey(i));
        int current_desc = termWidth(L(help[i].desc));
        if (current_key > longest_key) longest_key = current_key;
        if (current_desc > longest_desc) longest_desc = current_desc;
    }
    if (key_width) *key_width = longest_key;
    if (desc_width) *desc_width = longest_desc;
}

static void appendSpaces(int count) {
    while (count-- > 0) abAppend(" ");
}

static void clearScreen(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(E.hout, &csbi)) return;
    DWORD size = csbi.dwSize.X * csbi.dwSize.Y;
    COORD top_left = {0, 0};
    DWORD written;
    FillConsoleOutputCharacterA(E.hout, ' ', size, top_left, &written);
    FillConsoleOutputAttribute(E.hout, csbi.wAttributes, size, top_left, &written);
    SetConsoleCursorPosition(E.hout, top_left);
}

static void helpContentSize(int *rows, int *cols) {
    int total_rows = 3;
    int group_separators = 0;
    int key_width, desc_width;
    helpColumnWidths(&key_width, &desc_width);

    for (int i = 0; hasHelpEntry(i); i++) {
        total_rows++;
        if (!help[i].desc && i > 0) group_separators++;
        if (!help[i].desc && hasHelpEntry(i + 1)) group_separators++;
    }

    total_rows += group_separators + 3;
    if (rows) *rows = total_rows;
    if (cols) *cols = key_width + desc_width + 6;
}

void editorHelpMinimumSize(int *rows, int *cols) {
    int content_width;
    helpContentSize(NULL, &content_width);
    if (rows) *rows = 6;
    if (cols) *cols = content_width;
}

static void appendColored(const char *color, const char *text, int width) {
    abAppend(color);
    abAppend(text);
    abAppend(editorColor(COLOR_RESET));
    appendSpaces(width - termWidth(text));
}

static void appendColoredCentered(const char *color, const char *text, int width) {
    int text_width = termWidth(text);
    int left = (width - text_width) / 2;
    int right = width - text_width - left;
    abAppend(color);
    appendSpaces(left);
    abAppend(text);
    abAppend(editorColor(COLOR_RESET));
    appendSpaces(right);
}

static void appendIndent(int count) {
    appendSpaces(count);
}

static void appendBorder(int left, const char *start, const char *fill,
                         const char *end, int width) {
    appendIndent(left);
    abAppend(editorColor(COLOR_HELP_BORDER));
    abAppend(start);
    for (int i = 0; i < width; i++) abAppend(fill);
    abAppend(end);
    abAppend(editorColor(COLOR_RESET));
}

static void appendBottomBorder(int left, int width) {
    appendIndent(left);
    abAppend(editorColor(COLOR_HELP_BORDER));
    abAppend("└");
    for (int i = 0; i < width; i++) abAppend("─");
    abAppend("┘");
    abAppend(editorColor(COLOR_RESET));
}

static void appendHelpSide(const char *side) {
    abAppend(editorColor(COLOR_HELP_BORDER));
    abAppend(side);
    abAppend(editorColor(COLOR_RESET));
}

static int beginHelpRow(int document_row, int offset, int top_padding,
                        int screen_rows, int *screen_row) {
    *screen_row = top_padding + document_row - offset;
    if (*screen_row < 0 || *screen_row >= screen_rows) return 0;
    char position[32];
    snprintf(position, sizeof(position), "\x1b[%d;1H\x1b[2K",
             *screen_row + 1);
    abAppend(position);
    return 1;
}

static void helpBuildBuffer(int *scroll_offset, int *button_row,
                            int *button_left, int *button_width) {
    int total_rows, total_width;
    helpContentSize(&total_rows, &total_width);
    int key_width, desc_width;
    helpColumnWidths(&key_width, &desc_width);
    int inner_width = total_width - 2;
    int screen_cols = E.screencols;
    int screen_rows = E.screenrows + 3;
    if (screen_rows < 1) screen_rows = 1;

    int maximum_offset = total_rows - screen_rows;
    if (maximum_offset < 0) maximum_offset = 0;
    if (*scroll_offset > maximum_offset) *scroll_offset = maximum_offset;
    if (*scroll_offset < 0) *scroll_offset = 0;

    int left = (screen_cols - total_width) / 2;
    int vertical_pad = (screen_rows - total_rows) / 2;
    if (left < 0) left = 0;
    if (vertical_pad < 0) vertical_pad = 0;
    int row = 0;
    int visible_row;

    /* Clear each line immediately before replacing it.  The erase and its
       replacement stay adjacent in one WriteConsoleA call, avoiding the
       visible blank frame produced by clearScreen() during scrolling. */
    abAppend(editorColor(COLOR_RESET));
    abAppend("\x1b[?25l\x1b[H");
    for (int screen_row = 0; screen_row < screen_rows; screen_row++) {
        int document_row = screen_row - vertical_pad + *scroll_offset;
        if (document_row >= 0 && document_row < total_rows) continue;
        char position[32];
        snprintf(position, sizeof(position), "\x1b[%d;1H\x1b[2K",
                 screen_row + 1);
        abAppend(position);
    }
    *button_row = -1;

    if (beginHelpRow(row, *scroll_offset, vertical_pad, screen_rows,
                     &visible_row))
        appendBorder(left, "┌", "─", "┐", inner_width);
    row++;
    if (beginHelpRow(row, *scroll_offset, vertical_pad, screen_rows,
                     &visible_row)) {
        appendIndent(left);
        appendHelpSide("│");
        abAppend(" ");
        appendColoredCentered(editorColor(COLOR_HELP_TITLE),
                              L("help.title"),
                              inner_width - 2);
        abAppend(" ");
        appendHelpSide("│");
    }
    row++;
    if (beginHelpRow(row, *scroll_offset, vertical_pad, screen_rows,
                     &visible_row))
        appendBorder(left, "├", "─", "┤", inner_width);
    row++;

    for (int i = 0; hasHelpEntry(i); i++) {
        if (!help[i].desc && i > 0) {
            if (beginHelpRow(row, *scroll_offset, vertical_pad, screen_rows,
                             &visible_row))
                appendBorder(left, "├", "─", "┤", inner_width);
            row++;
        }

        if (beginHelpRow(row, *scroll_offset, vertical_pad, screen_rows,
                         &visible_row)) {
            appendIndent(left);
            appendHelpSide("│");
            abAppend(" ");
            if (!help[i].desc) {
                appendColoredCentered(editorColor(COLOR_HELP_SECTION),
                                      helpEntryKey(i),
                                      inner_width - 2);
            } else {
                appendColored(editorColor(COLOR_HELP_KEY),
                              helpEntryKey(i), key_width);
                appendSpaces(2);
                appendColored(editorColor(COLOR_HELP_DESCRIPTION),
                              L(help[i].desc), desc_width);
            }
            abAppend(" ");
            appendHelpSide("│");
        }
        row++;

        if (!help[i].desc && hasHelpEntry(i + 1)) {
            if (beginHelpRow(row, *scroll_offset, vertical_pad, screen_rows,
                             &visible_row))
                appendBorder(left, "├", "─", "┤", inner_width);
            row++;
        }
    }

    if (beginHelpRow(row, *scroll_offset, vertical_pad, screen_rows,
                     &visible_row))
        appendBorder(left, "├", "─", "┤", inner_width);
    row++;
    const char *button = L("help.back");
    *button_width = termWidth(button);
    *button_left = left + (inner_width - *button_width) / 2 + 1;

    if (beginHelpRow(row, *scroll_offset, vertical_pad, screen_rows,
                     &visible_row)) {
        *button_row = visible_row;
        appendIndent(left);
        appendHelpSide("│");
        int pad = (inner_width - *button_width) / 2;
        appendSpaces(pad);
        abAppend(editorColor(COLOR_HELP_BUTTON));
        abAppend(button);
        abAppend(editorColor(COLOR_RESET));
        appendSpaces(inner_width - pad - *button_width);
        appendHelpSide("│");
    }
    row++;
    if (beginHelpRow(row, *scroll_offset, vertical_pad, screen_rows,
                     &visible_row))
        appendBottomBorder(left, inner_width);
}

static void helpDrawButton(int row, int left, int hover) {
    char buffer[128];
    int length;
    if (hover)
        length = snprintf(buffer, sizeof(buffer),
                          "\x1b[%d;%dH%s%s%s%s",
                          row + 1, left + 1,
                          editorColor(COLOR_HELP_BUTTON_HOVER_BACKGROUND),
                          editorColor(COLOR_HELP_BUTTON_HOVER_TEXT), L("help.back"),
                          editorColor(COLOR_RESET));
    else
        length = snprintf(buffer, sizeof(buffer),
                          "\x1b[%d;%dH%s%s%s",
                          row + 1, left + 1,
                          editorColor(COLOR_HELP_BUTTON), L("help.back"),
                          editorColor(COLOR_RESET));
    DWORD written;
    WriteConsoleA(E.hout, buffer, length, &written, NULL);
}

void editorHelp(void) {
    int button_row, button_left, button_width;
    int was_hover = 0;
    int scroll_offset = 0;

    editorFitBufferToWindow();
    helpBuildBuffer(&scroll_offset, &button_row, &button_left, &button_width);
    abWrite();
    abFree();

    for (;;) {
        int key = editorReadKey();
        if (key == ACTION_CANCEL) break;

        if (key == WINDOW_RESIZED || key == WHEEL_UP || key == WHEEL_DOWN ||
            key == ACTION_MOVE_UP || key == ACTION_MOVE_DOWN ||
            key == ACTION_PAGE_UP || key == ACTION_PAGE_DOWN) {
            int previous_offset = scroll_offset;
            int total_rows;
            helpContentSize(&total_rows, NULL);
            int visible_rows = E.screenrows + 3;
            if (visible_rows < 1) visible_rows = 1;
            int maximum_offset = total_rows - visible_rows;
            if (maximum_offset < 0) maximum_offset = 0;

            if (key == WHEEL_UP || key == ACTION_MOVE_UP)
                scroll_offset--;
            else if (key == WHEEL_DOWN || key == ACTION_MOVE_DOWN)
                scroll_offset++;
            else if (key == ACTION_PAGE_UP)
                scroll_offset -= visible_rows;
            else if (key == ACTION_PAGE_DOWN)
                scroll_offset += visible_rows;
            if (scroll_offset < 0) scroll_offset = 0;
            if (scroll_offset > maximum_offset) scroll_offset = maximum_offset;
            if (key != WINDOW_RESIZED && scroll_offset == previous_offset)
                continue;

            was_hover = 0;
            helpBuildBuffer(&scroll_offset, &button_row,
                            &button_left, &button_width);
            abWrite();
            abFree();
            continue;
        }

        if (key == MOUSE_HOVER) {
            int hover = E.mouse_y == button_row &&
                        E.mouse_x >= button_left &&
                        E.mouse_x < button_left + button_width;
            if (hover != was_hover) {
                helpDrawButton(button_row, button_left, hover);
                was_hover = hover;
            }
            continue;
        }

        if (key == MOUSE_PRESS && E.mouse_y == button_row &&
            E.mouse_x >= button_left &&
            E.mouse_x < button_left + button_width)
            break;
    }

    clearScreen();
    editorRefreshScreen();
}
