#include "common.h"
#include "undo.h"
#include "row.h"
#include "syntax.h"

/* ---- cursor ->render position conversion ---- */
int editorRowCxToRx(erow *row, int cx) {
    int rx = 0, j = 0;
    while (j < cx && j < row->size) {
        unsigned char c = (unsigned char)row->chars[j];
        int clen = utf8_char_len(c);
        if (j + clen > row->size) clen = row->size - j;
        if (row->chars[j] == '\t')
            rx += E.tab_width - (rx % E.tab_width);
        else
            rx += utf8_display_width(&row->chars[j]);
        j += clen;
    }
    return rx;
}

int editorRowCxToRenderIndex(erow *row, int cx) {
    int render_index = 0;
    int display_column = 0;
    for (int index = 0; index < cx && index < row->size;) {
        unsigned char value = (unsigned char)row->chars[index];
        int length = utf8_char_len(value);
        if (index + length > row->size) length = row->size - index;
        if (row->chars[index] == '\t') {
            int spaces = E.tab_width - (display_column % E.tab_width);
            render_index += spaces;
            display_column += spaces;
        } else {
            render_index += length;
            display_column += utf8_display_width(&row->chars[index]);
        }
        index += length;
    }
    return render_index;
}

int editorRowRxToCx(erow *row, int rx) {
    int cur_rx = 0, cx = 0;
    while (cx < row->size) {
        unsigned char c = (unsigned char)row->chars[cx];
        int clen = utf8_char_len(c);
        if (cx + clen > row->size) clen = row->size - cx;
        if (row->chars[cx] == '\t')
            cur_rx += E.tab_width - (cur_rx % E.tab_width);
        else
            cur_rx += utf8_display_width(&row->chars[cx]);
        if (cur_rx > rx) return cx;
        cx += clen;
    }
    return row->size;
}

static int isIdentifierStart(unsigned char value) {
    return isalpha(value) || value == '_';
}

static int isIdentifierPart(unsigned char value) {
    return isalnum(value) || value == '_';
}

/* ---- syntax highlighting ---- */
void editorUpdateSyntax(erow *row) {
    int old_open_state = row->hl_open_state;
    row->hl = realloc(row->hl, row->rsize);
    if (row->hl == NULL && row->rsize != 0) die("realloc");
    memset(row->hl, HL_NORMAL, row->rsize);
    row->hl_open_state = 0;
    if (!editorSyntaxEnabled()) {
        if (old_open_state != 0 && row->idx + 1 < E.numrows)
            editorUpdateSyntax(&E.row[row->idx + 1]);
        return;
    }

    const char *line_comment = editorSyntaxLineComment();
    int comment_length = (int)strlen(line_comment);
    const char *block_start = editorSyntaxBlockCommentStart();
    const char *block_end = editorSyntaxBlockCommentEnd();
    int block_start_length = (int)strlen(block_start);
    int block_end_length = (int)strlen(block_end);
    const char *section_start = editorSyntaxSectionStart();
    const char *section_end = editorSyntaxSectionEnd();
    int section_start_length = (int)strlen(section_start);
    int section_end_length = (int)strlen(section_end);
    int first_nonspace = 0;
    while (first_nonspace < row->rsize &&
           isspace((unsigned char)row->render[first_nonspace]))
        first_nonspace++;

    int prev_sep  = 1;
    int state = row->idx > 0 ? E.row[row->idx - 1].hl_open_state : 0;
    int i = 0;

    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        if (state == 1) {
            if (block_end_length > 0 &&
                i + block_end_length <= row->rsize &&
                strncmp(&row->render[i], block_end,
                        block_end_length) == 0) {
                memset(&row->hl[i], HL_COMMENT, block_end_length);
                i += block_end_length;
                state = 0;
                prev_sep = 1;
            } else {
                row->hl[i++] = HL_COMMENT;
            }
            continue;
        }

        if (state >= 2) {
            const char *delimiter =
                editorSyntaxMultilineStringAt(state - 2);
            int delimiter_length = (int)strlen(delimiter);
            if (delimiter_length > 0 &&
                i + delimiter_length <= row->rsize &&
                strncmp(&row->render[i], delimiter,
                        delimiter_length) == 0) {
                memset(&row->hl[i], HL_STRING, delimiter_length);
                i += delimiter_length;
                state = 0;
                prev_sep = 1;
            } else {
                row->hl[i++] = HL_STRING;
            }
            continue;
        }

        if (comment_length > 0 &&
            i + comment_length <= row->rsize &&
            strncmp(&row->render[i], line_comment, comment_length) == 0) {
            memset(&row->hl[i], HL_COMMENT, row->rsize - i);
            break;
        }

        if (block_start_length > 0 &&
            i + block_start_length <= row->rsize &&
            strncmp(&row->render[i], block_start,
                    block_start_length) == 0) {
            memset(&row->hl[i], HL_COMMENT, block_start_length);
            i += block_start_length;
            state = 1;
            continue;
        }

        int multiline_started = 0;
        for (int delimiter_index = 0;
             delimiter_index < editorSyntaxMultilineStringCount();
             delimiter_index++) {
            const char *delimiter =
                editorSyntaxMultilineStringAt(delimiter_index);
            int delimiter_length = (int)strlen(delimiter);
            if (delimiter_length > 0 &&
                i + delimiter_length <= row->rsize &&
                strncmp(&row->render[i], delimiter,
                        delimiter_length) == 0) {
                memset(&row->hl[i], HL_STRING, delimiter_length);
                i += delimiter_length;
                state = delimiter_index + 2;
                multiline_started = 1;
                break;
            }
        }
        if (multiline_started) continue;

        if (i == first_nonspace && section_start_length > 0 &&
            section_end_length > 0 &&
            i + section_start_length <= row->rsize &&
            strncmp(&row->render[i], section_start,
                    section_start_length) == 0) {
            char *end = strstr(&row->render[i + section_start_length],
                               section_end);
            if (end) {
                int length = (int)(end - &row->render[i]) +
                             section_end_length;
                memset(&row->hl[i], HL_SECTION, length);
                i += length;
                prev_sep = 1;
                continue;
            }
        }

        if (c == '"' || c == '\'') {
            char quote = c;
            int escaped = 0;
            row->hl[i++] = HL_STRING;
            while (i < row->rsize) {
                char value = row->render[i];
                row->hl[i++] = HL_STRING;
                if (!escaped && value == quote) break;
                if (!escaped && value == '\\') escaped = 1;
                else escaped = 0;
            }
            prev_sep = 1;
            continue;
        }

        if ((isdigit((unsigned char)c) && (prev_sep || prev_hl == HL_NUMBER))
            || (c == '.' && prev_hl == HL_NUMBER)) {
            row->hl[i] = HL_NUMBER;
            i++; prev_sep = 0;
            continue;
        }

        if (prev_sep) {
            int keyword_length = 0;
            int highlight = editorSyntaxKeywordMatch(
                &row->render[i], &keyword_length);
            if (highlight != HL_NORMAL) {
                memset(&row->hl[i], highlight, keyword_length);
                i += keyword_length;
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(c);
        i++;
    }

    row->hl_open_state = state;

    /* Any otherwise-unclassified identifier is a user symbol.  A symbol
       followed by optional whitespace and '(' is treated as a function. */
    i = 0;
    while (i < row->rsize) {
        if (row->hl[i] != HL_NORMAL ||
            !isIdentifierStart((unsigned char)row->render[i])) {
            i++;
            continue;
        }
        int start = i;
        while (i < row->rsize &&
               isIdentifierPart((unsigned char)row->render[i]) &&
               row->hl[i] == HL_NORMAL)
            i++;
        int end = i;
        while (i < row->rsize && isspace((unsigned char)row->render[i])) i++;
        unsigned char highlight =
            i < row->rsize && row->render[i] == '('
                ? HL_FUNCTION : HL_VARIABLE;
        memset(&row->hl[start], highlight, end - start);
        i = end;
    }

    if (old_open_state != row->hl_open_state &&
        row->idx + 1 < E.numrows)
        editorUpdateSyntax(&E.row[row->idx + 1]);
}

/* ---- row lifecycle ---- */
void editorUpdateRow(erow *row) {
    int tabs = 0;
    for (int j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (E.tab_width - 1) + 1);
    if (!row->render) die("malloc");

    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % E.tab_width != 0)
                row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;

    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    if (!E.row) die("realloc");

    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
    for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

    E.row[at].idx    = at;
    E.row[at].size   = (int)len;
    E.row[at].chars  = malloc(len + 1);
    if (!E.row[at].chars) die("malloc");
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.row[at].rsize  = 0;
    E.row[at].render = NULL;
    E.row[at].hl     = NULL;
    E.row[at].hl_open_state = 0;

    editorUpdateRow(&E.row[at]);
    E.numrows++;
    editorUpdateSyntax(&E.row[at]);
    E.dirty++;
    editorUpdateLineNumWidth();
}

void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
    E.numrows--;
    if (at < E.numrows) editorUpdateSyntax(&E.row[at]);
    E.dirty++;
    editorUpdateLineNumWidth();
}

/* ---- character-level row editing ---- */
void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    if (!undo_suspended)
        editorPushUndo(UO_INSERT_CHAR, row->idx, at, row->idx, at, NULL, 0);

    row->chars = realloc(row->chars, row->size + 2);
    if (!row->chars) die("realloc");
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = (char)c;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size) return;

    int start = at;
    /* walk back to the start of the UTF-8 character at `at` */
    while (start > 0 && is_utf8_cont((unsigned char)row->chars[start]))
        start--;

    int clen = utf8_char_len((unsigned char)row->chars[start]);
    if (start + clen > row->size) clen = row->size - start;

    if (!undo_suspended)
        editorPushUndo(UO_DELETE_CHAR, row->idx, start, row->idx, start,
                       &row->chars[start], clen);
    memmove(&row->chars[start], &row->chars[start + clen],
            row->size - (start + clen));
    row->size -= clen;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    if (!row->chars) die("realloc");
    memcpy(&row->chars[row->size], s, len);
    row->size += (int)len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

/* ---- serialise all rows ---- */
char *editorRowsToString(int *buflen) {
    int totlen = 0;
    for (int j = 0; j < E.numrows; j++)
        totlen += E.row[j].size + 2;
    *buflen = totlen;

    char *buf = malloc(totlen + 1);
    if (!buf) die("malloc");

    char *p = buf;
    for (int j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p++ = '\r'; *p++ = '\n';
    }
    *p = '\0';
    return buf;
}

/* ---- line-number gutter width ---- */
void editorUpdateLineNumWidth(void) {
    int w = 1, n = E.numrows ? E.numrows : 1;
    while (n >= 10) { n /= 10; w++; }
    E.linenum_width = w < 3 ? 3 : w;
}
