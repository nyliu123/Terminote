#include "common.h"
#include "terminal.h"
#include "row.h"
#include "editor.h"
#include "output.h"
#include "colors.h"
#include "localization.h"
#include "bracket.h"
#include "keybindings.h"
#include "banner.h"

/* ---- append buffer ---- */
static struct abuf G_ab = {NULL, 0};

void abAppend(const char *s) {
    int len = (int)strlen(s);
    char *newbuf = realloc(G_ab.b, G_ab.len + len + 1);
    if (!newbuf) return;
    memcpy(&newbuf[G_ab.len], s, len);
    G_ab.b = newbuf;
    G_ab.len += len;
    G_ab.b[G_ab.len] = '\0';
}

void abFree(void) {
    free(G_ab.b);
    G_ab.b = NULL;
    G_ab.len = 0;
}

void abWrite(void) {
    DWORD written;
    if (G_ab.b && G_ab.len > 0)
        WriteConsoleA(E.hout, G_ab.b, G_ab.len, &written, NULL);
}

/* ---- draw text rows ---- */
static void appendRowBaseColors(int on_cursor) {
    abAppend(editorColor(COLOR_RESET));
    if (on_cursor) abAppend(editorColor(COLOR_ACTIVE_LINE_BACKGROUND));
    abAppend(editorColor(COLOR_TEXT));
}

static void appendSelectionColors(void) {
    abAppend(editorColor(COLOR_SELECTION));
    abAppend(editorColor(COLOR_SELECTION_TEXT));
}

enum {
    WELCOME_BANNER_LINE = 1,
    WELCOME_DESCRIPTION_LINE = WELCOME_BANNER_LINE + BANNER_HEIGHT + 1,
    WELCOME_SHORTCUT_LINE = WELCOME_DESCRIPTION_LINE + 2,
    WELCOME_LINE_COUNT = WELCOME_SHORTCUT_LINE + 1
};

static int welcomeCharWidth(const char *text, int *byte_length) {
    return utf8_terminal_char_width(text, byte_length);
}

static int welcomeTextWidth(const char *text) {
    return utf8_terminal_width(text);
}

static void appendWelcomeText(const char *text, int max_width) {
    int used = 0;
    while (*text) {
        int length;
        int width = welcomeCharWidth(text, &length);
        if (used + width > max_width) break;

        char character[5];
        memcpy(character, text, length);
        character[length] = '\0';
        abAppend(character);

        text += length;
        used += width;
    }
}

static void appendCenteredWelcomeText(const char *text, int text_width,
                                      int max_width) {
    if (text_width <= max_width) {
        int padding = (max_width - text_width) / 2;
        while (padding-- > 0) abAppend(" ");
        appendWelcomeText(text, max_width);
        return;
    }

    int target_skip = (text_width - max_width) / 2;
    int skipped = 0;
    while (*text && skipped < target_skip) {
        int length;
        skipped += welcomeCharWidth(text, &length);
        text += length;
    }

    /* A two-column character can straddle the ideal crop boundary.  Skip it
       whole and compensate with a leading space so the remaining text stays
       geometrically centered without emitting a partial UTF-8 sequence. */
    int leading = skipped - target_skip;
    if (leading > max_width) leading = max_width;
    for (int i = 0; i < leading; i++) abAppend(" ");
    appendWelcomeText(text, max_width - leading);
}

static void editorDrawWelcomeLine(int index) {
    char shortcuts[160];
    snprintf(shortcuts, sizeof(shortcuts),
             L("welcome.shortcuts"),
             editorBindingLabel(ACTION_HELP),
             editorBindingLabel(ACTION_OPEN),
             editorBindingLabel(ACTION_NEW_FILE));
    if (index < 0 || index >= WELCOME_LINE_COUNT) return;

    const char *text = "";
    if (index >= WELCOME_BANNER_LINE &&
        index < WELCOME_BANNER_LINE + BANNER_HEIGHT)
        text = bannerLine(index - WELCOME_BANNER_LINE);
    else if (index == WELCOME_DESCRIPTION_LINE)
        text = L("welcome.description");
    else if (index == WELCOME_SHORTCUT_LINE)
        text = shortcuts;

    if (index >= WELCOME_BANNER_LINE &&
        index < WELCOME_BANNER_LINE + BANNER_HEIGHT)
        abAppend(editorColor(COLOR_WELCOME_LOGO));
    else if (index == WELCOME_DESCRIPTION_LINE)
        abAppend(editorColor(COLOR_WELCOME_DESCRIPTION));
    else if (index == WELCOME_SHORTCUT_LINE)
        abAppend(editorColor(COLOR_WELCOME_SHORTCUT));

    int width = welcomeTextWidth(text);
    appendCenteredWelcomeText(text, width, E.screencols);
    abAppend(editorColor(COLOR_TEXT));
}

static int welcomeVisibleLineCount(int available_rows) {
    if (available_rows < 1) return 0;
    return available_rows < WELCOME_LINE_COUNT
        ? available_rows : WELCOME_LINE_COUNT;
}

/* On short windows, discard decorative blank rows first.  If still short,
   retain as much of the logo as possible while always keeping the version
   description and shortcut line visible. */
static int welcomeLineAt(int visible_index, int visible_count) {
    if (visible_count >= WELCOME_LINE_COUNT) return visible_index;
    if (visible_count == WELCOME_LINE_COUNT - 1) return visible_index + 1;
    if (visible_count == 1) return WELCOME_SHORTCUT_LINE;
    if (visible_index < visible_count - 2)
        return WELCOME_BANNER_LINE + visible_index;
    return visible_index == visible_count - 2
        ? WELCOME_DESCRIPTION_LINE : WELCOME_SHORTCUT_LINE;
}

/* return the number of visible content rows, subtracting 1 for
   the horizontal scrollbar when it has a non-zero moving range */
int editorMaxRenderWidth(int minimum) {
    int maximum = 1;
    for (int i = 0; i < E.numrows; i++) {
        int width = editorRowCxToRx(&E.row[i], E.row[i].size);
        if (width > maximum) maximum = width;
    }
    return maximum < minimum ? minimum : maximum;
}

int editorVisibleRows(void) {
    int text_cols = E.screencols - (E.linenum_width + 1);
    if (text_cols < 1) text_cols = 1;

    int max_rx = editorMaxRenderWidth(text_cols);
    int rows = E.screenrows - (max_rx > text_cols ? 1 : 0);
    return rows < 1 ? 1 : rows;
}

static void drawLineNumber(int filerow, int on_cursor) {
    char text[32];
    if (on_cursor)
        snprintf(text, sizeof(text), "%s%s%*d%s ",
                 editorColor(COLOR_ACTIVE_LINE_BACKGROUND),
                 editorColor(COLOR_ACTIVE_LINE_NUMBER),
                 E.linenum_width, filerow + 1, editorColor(COLOR_TEXT));
    else
        snprintf(text, sizeof(text), "%s%*d%s ",
                 editorColor(COLOR_LINE_NUMBER), E.linenum_width,
                 filerow + 1, editorColor(COLOR_TEXT));
    abAppend(text);
}

static void selectionBounds(erow *row, int filerow, int *start, int *end) {
    *start = *end = -1;
    if (!E.sel_active || E.numrows <= 0) return;

    int sr = E.sel_anchor_row, sc = E.sel_anchor_col;
    int er = E.cy, ec = E.cx;
    if (er >= E.numrows) er = E.numrows - 1;
    if (sr >= E.numrows) sr = E.numrows - 1;
    if (sc > E.row[sr].size) sc = E.row[sr].size;
    if (ec > E.row[er].size) ec = E.row[er].size;
    if (er < sr || (er == sr && ec < sc)) {
        int swap = sr; sr = er; er = swap;
        swap = sc; sc = ec; ec = swap;
    }
    if (filerow < sr || filerow > er) return;
    *start = filerow == sr ? editorRowCxToRx(row, sc) : 0;
    *end = filerow == er ? editorRowCxToRx(row, ec) : 999999;
    if (*start > *end) {
        int swap = *start; *start = *end; *end = swap;
    }
}

static void drawHorizontalScrollbar(int gutter_width, int text_width,
                                    int max_render) {
    double ratio = (double)text_width / max_render;
    int thumb_width = (int)(text_width * ratio);
    if (thumb_width < 2) thumb_width = 2;
    if (thumb_width > text_width) thumb_width = text_width;

    int max_offset = max_render - text_width + 1;
    if (max_offset < 0) max_offset = 0;
    int thumb_x = max_offset > 0
        ? (int)((double)E.coloff / max_offset *
                (text_width - thumb_width)) : 0;
    if (thumb_x < 0) thumb_x = 0;
    if (thumb_x + thumb_width > text_width)
        thumb_x = text_width - thumb_width;

    for (int i = 0; i < gutter_width; i++) abAppend(" ");
    abAppend(editorColor(COLOR_SCROLLBAR_TRACK));
    for (int i = 0; i < thumb_x; i++) abAppend(" ");
    abAppend(editorColor(COLOR_SCROLLBAR_THUMB));
    for (int i = 0; i < thumb_width; i++) abAppend(" ");
    abAppend(editorColor(COLOR_SCROLLBAR_TRACK));
    for (int i = thumb_x + thumb_width; i < text_width; i++) abAppend(" ");
    abAppend(editorColor(COLOR_RESET));
    abAppend("\x1b[K\r\n");
}

void editorDrawRows(void) {
    int gutter_w  = E.linenum_width + 1;
    int text_cols = E.screencols - gutter_w;
    if (text_cols < 1) text_cols = 1;

    int max_rx = editorMaxRenderWidth(text_cols);

    int hscroll_needed = (max_rx > text_cols);

    int content_rows = editorVisibleRows();
    if (content_rows < 1) content_rows = 1;

    for (int y = 0; y < content_rows; y++) {
        int filerow = y + E.rowoff;
        int on_cursor = (filerow < E.numrows && filerow == E.cy);

        if (filerow >= E.numrows) {
            /* welcome screen or blank line --no gutter when empty */
            if (E.numrows == 0 && !E.welcome_dismissed) {
                int welcome_count = welcomeVisibleLineCount(content_rows);
                int welcome_start = (content_rows - welcome_count) / 2;
                if (welcome_start < 0) welcome_start = 0;
                int visible_line = y - welcome_start;
                if (visible_line >= 0 && visible_line < welcome_count)
                    editorDrawWelcomeLine(
                        welcomeLineAt(visible_line, welcome_count));
            }
            /* ponytail: blank rows below welcome just emit \x1b[K\r\n */
        } else {
            erow *row  = &E.row[filerow];
            drawLineNumber(filerow, on_cursor);

            /* skip off-screen columns (character-boundary aware) */
            int bo = 0, col = 0;
            while (bo < row->rsize && col < E.coloff) {
                unsigned char c = (unsigned char)row->render[bo];
                if (c < 0x80) { bo++; col++; }
                else {
                    int clen = utf8_seq_len(c);
                    if (bo + clen > row->rsize) clen = row->rsize - bo;
                    bo += clen;
                    col += 2;
                }
            }

            /* pre-compute selection bounds in render-space */
            int sel_start_rx, sel_end_rx;
            selectionBounds(row, filerow, &sel_start_rx, &sel_end_rx);

            int current_color = -1;
            int in_match      = 0;
            int in_comment    = 0;
            int in_select     = (sel_start_rx >= 0 && col >= sel_start_rx
                                 && col < sel_end_rx) ? 1 : 0;
            int disp_cols     = 0;

            if (in_select) appendSelectionColors();

            while (bo < row->rsize && disp_cols < text_cols) {
                unsigned char c = (unsigned char)row->render[bo];
                int clen, cwidth;
                if (c < 0x80) { clen = 1; cwidth = 1; }
                else {
                    clen   = utf8_seq_len(c);
                    if (bo + clen > row->rsize) clen = row->rsize - bo;
                    cwidth = 2;
                }
                if (disp_cols + cwidth > text_cols) break;

                /* selection toggle at char boundary */
                if (sel_start_rx >= 0) {
                    int was_sel = in_select;
                    in_select = (col >= sel_start_rx && col < sel_end_rx) ? 1 : 0;
                    if (in_select && !was_sel) {
                        if (in_comment) { abAppend("\x1b[23m"); in_comment = 0; }
                        if (in_match)       { appendRowBaseColors(on_cursor); in_match = 0; }
                        if (current_color != -1) { abAppend(editorColor(COLOR_TEXT)); current_color = -1; }
                        appendSelectionColors();
                    } else if (!in_select && was_sel) {
                        appendRowBaseColors(on_cursor);
                        current_color = -1;
                        in_match = 0;
                        in_comment = 0;
                    }
                }

                if (in_select) {
                    char tmp[5];
                    memcpy(tmp, &row->render[bo], clen);
                    tmp[clen] = 0;
                    abAppend(tmp);
                    bo  += clen; col += cwidth; disp_cols += cwidth;
                    continue;
                }

                /* control characters */
                if (c < 0x20 || c == 0x7F) {
                    char sym = (c <= 26) ? '@' + c : '?';
                    if (in_comment) { abAppend("\x1b[23m"); in_comment = 0; }
                    if (in_match) { appendRowBaseColors(on_cursor); in_match = 0; }
                    appendSelectionColors();
                    { char tmp[2] = {sym, 0}; abAppend(tmp); }
                    appendRowBaseColors(on_cursor);
                    if (current_color != -1) {
                        abAppend(editorColorSequenceForCode(current_color));
                    }
                    bo++; col++; disp_cols++;
                    continue;
                }

                /* syntax / match highlighting */
                unsigned char hl_val = row->hl[bo];
                if (hl_val == HL_NORMAL) {
                    if (in_comment) { abAppend("\x1b[23m"); in_comment = 0; }
                    if (in_match) { appendRowBaseColors(on_cursor); in_match = 0; }
                    if (current_color != -1) {
                        abAppend(editorColor(COLOR_TEXT));
                        current_color = -1;
                    }
                } else if (hl_val == HL_MATCH) {
                    if (in_comment) { abAppend("\x1b[23m"); in_comment = 0; }
                    if (current_color != -1) {
                        abAppend(editorColor(COLOR_TEXT));
                        current_color = -1;
                    }
                    if (!in_match) { abAppend(editorColor(COLOR_MATCH)); in_match = 1; }
                } else {
                    if (in_match) {
                        appendRowBaseColors(on_cursor);
                        in_match = 0;
                        current_color = -1;
                        in_comment = 0;
                    }
                    if (hl_val == HL_COMMENT && !in_comment) {
                        abAppend("\x1b[3m");
                        in_comment = 1;
                    } else if (hl_val != HL_COMMENT && in_comment) {
                        abAppend("\x1b[23m");
                        in_comment = 0;
                    }
                    int color = editorSyntaxToColor(hl_val);
                    if (color != current_color) {
                        current_color = color;
                        abAppend(editorColorSequenceForCode(color));
                    }
                }

                int bracket = editorBracketMatchedAt(filerow, bo);
                if (bracket) {
                    abAppend(editorColor(COLOR_BRACKET_MATCH_BACKGROUND));
                    abAppend(editorColor(COLOR_BRACKET_MATCH_TEXT));
                }
                { char tmp[5]; memcpy(tmp, &row->render[bo], clen); tmp[clen] = 0; abAppend(tmp); }
                if (bracket) {
                    appendRowBaseColors(on_cursor);
                    if (in_comment) abAppend("\x1b[3m");
                    if (in_match) {
                        abAppend(editorColor(COLOR_MATCH));
                    } else if (current_color != -1) {
                        abAppend(editorColorSequenceForCode(current_color));
                    }
                }
                bo  += clen; col += cwidth; disp_cols += cwidth;
            }

            if (in_select)        appendRowBaseColors(on_cursor);
            if (in_match)         appendRowBaseColors(on_cursor);
            if (in_comment)       abAppend("\x1b[23m");
            if (current_color != -1) abAppend(editorColor(COLOR_TEXT));
        }

        if (filerow < E.numrows && filerow == E.cy) {
            /* cursor line: fill rest of line with grey bg, then clear */
            abAppend("\x1b[K");
            abAppend(editorColor(COLOR_RESET));
        } else {
            abAppend("\x1b[K");
            abAppend(editorColor(COLOR_RESET));
        }
        abAppend("\r\n");
    }

    /* -- horizontal scrollbar (last content row) ------------- */
    if (hscroll_needed) drawHorizontalScrollbar(gutter_w, text_cols, max_rx);
}

