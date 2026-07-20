#include "common.h"
#include "row.h"
#include "undo.h"
#include "editor.h"

/* ---- character insertion / deletion ---- */
void editorInsertChar(int c) {
    E.welcome_dismissed = 1;
    if (E.cy == E.numrows) {
        if (!undo_suspended)
            editorPushUndo(UO_INSERT_ROW, E.numrows, 0, E.cy, E.cx, NULL, 0);
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertNewline(void) {
    E.welcome_dismissed = 1;
    if (E.cx == 0) {
        if (!undo_suspended)
            editorPushUndo(UO_INSERT_ROW, E.cy, 0, E.cy, E.cx, NULL, 0);
        editorInsertRow(E.cy, "", 0);
    } else {
        if (!undo_suspended)
            editorPushUndo(UO_SPLIT_ROW, E.cy, E.cx, E.cy, E.cx, NULL, 0);
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

static void editorInsertAtomicText(const char *text, int length,
                                   int result_row, int result_col) {
    int start_row = E.cy, start_col = E.cx;
    char *owned = malloc(length + 1);
    if (!owned) die("malloc: atomic text");
    memcpy(owned, text, length);
    owned[length] = '\0';

    undo_suspended++;
    editorInsertBlock(owned, length);
    if (undo_suspended > 0) undo_suspended--;
    int end_row = E.cy, end_col = E.cx;

    editorPushUndoBlock(UO_ATOMIC_INSERT,
                        start_row, start_col, end_row, end_col,
                        start_row, start_col, owned, length);
    if (result_row >= 0) {
        editorSetLastUndoResultCursor(result_row, result_col);
        E.cy = result_row;
        E.cx = result_col;
    }
}

void editorInsertConfiguredTab(void) {
    char *spaces = malloc(E.tab_width + 1);
    if (!spaces) die("malloc: tab spaces");
    memset(spaces, ' ', E.tab_width);
    spaces[E.tab_width] = '\0';
    editorInsertAtomicText(spaces, E.tab_width, -1, -1);
    free(spaces);
}

static char closingBracket(char opener) {
    switch (opener) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case '\"': return '\"';
        case '\'': return '\'';
        default: return '\0';
    }
}

static char openingBracket(char closer) {
    switch (closer) {
        case ')': return '(';
        case ']': return '[';
        case '}': return '{';
        default: return '\0';
    }
}

static int bracketIsCode(erow *row, int column) {
    int render = editorRowCxToRenderIndex(row, column);
    if (render < 0 || render >= row->rsize || !row->hl) return 0;
    return row->hl[render] != HL_COMMENT && row->hl[render] != HL_STRING;
}

static int findOpeningBracket(char closer, int *match_row, int *match_col) {
    char opener = openingBracket(closer);
    if (!opener) return 0;

    int depth = 0;
    for (int row_index = E.cy; row_index >= 0; row_index--) {
        erow *row = &E.row[row_index];
        int column = row_index == E.cy ? E.cx : row->size;
        while (column > 0) {
            column = editorPrevCharByte(row, column);
            if (!bracketIsCode(row, column)) continue;
            char value = row->chars[column];
            if (value == closer) {
                depth++;
            } else if (value == opener) {
                if (depth == 0) {
                    *match_row = row_index;
                    *match_col = column;
                    return 1;
                }
                depth--;
            }
        }
    }
    return 0;
}

/* If a closer is typed on an indentation-only prefix, align it with the
   first non-whitespace character on the matching opener's line. */
static int editorInsertAlignedCloser(char closer) {
    if (!E.auto_indent || E.cy < 0 || E.cy >= E.numrows ||
        !openingBracket(closer))
        return 0;

    erow *current = &E.row[E.cy];
    for (int i = 0; i < E.cx; i++) {
        if (current->chars[i] != ' ' && current->chars[i] != '\t') return 0;
    }

    int opener_row, opener_col;
    if (!findOpeningBracket(closer, &opener_row, &opener_col)) return 0;
    (void)opener_col;

    erow *opening_row = &E.row[opener_row];
    int target_length = 0;
    while (target_length < opening_row->size &&
           (opening_row->chars[target_length] == ' ' ||
            opening_row->chars[target_length] == '\t'))
        target_length++;

    int old_length = E.cx;
    if (old_length == target_length &&
        (old_length == 0 ||
         memcmp(current->chars, opening_row->chars, old_length) == 0))
        return 0;

    int existing_closer = E.cx < current->size &&
                          current->chars[E.cx] == closer;
    int new_length = target_length + (existing_closer ? 0 : 1);
    char *old_text = malloc(old_length + 1);
    char *new_text = malloc(new_length + 1);
    if (!old_text || !new_text) die("malloc: aligned closing bracket");
    if (old_length > 0) memcpy(old_text, current->chars, old_length);
    old_text[old_length] = '\0';
    if (target_length > 0)
        memcpy(new_text, opening_row->chars, target_length);
    if (!existing_closer) new_text[target_length] = closer;
    new_text[new_length] = '\0';

    int row_index = E.cy;
    undo_suspended++;
    editorDeleteRange(row_index, 0, row_index, old_length);
    E.cy = row_index;
    E.cx = 0;
    editorInsertBlock(new_text, new_length);
    if (undo_suspended > 0) undo_suspended--;

    editorPushUndoAtomicReplace(row_index, 0,
                                row_index, old_length,
                                row_index, new_length,
                                row_index, old_length,
                                old_text, old_length,
                                new_text, new_length);
    editorSetLastUndoResultCursor(row_index, target_length + 1);
    E.cy = row_index;
    E.cx = target_length + 1;
    return 1;
}

int editorInsertAutoPair(int c) {
    if (editorInsertAlignedCloser((char)c)) return 1;
    if (!E.auto_pair) return 0;

    if (E.cy < E.numrows) {
        erow *row = &E.row[E.cy];
        if (E.cx < row->size && row->chars[E.cx] == c &&
            (c == ')' || c == ']' || c == '}' ||
             c == '\"' || c == '\'')) {
            E.cx++;
            return 1;
        }
        if ((c == '\"' || c == '\'') && E.cx > 0 &&
            row->chars[E.cx - 1] == '\\')
            return 0;
    }

    char closer = closingBracket((char)c);
    if (!closer) return 0;

    char pair[3] = {(char)c, closer, '\0'};
    int result_row = E.cy;
    int result_col = E.cx + 1;
    editorInsertAtomicText(pair, 2, result_row, result_col);
    return 1;
}

void editorInsertNewlineWithIndent(void) {
    if (!E.auto_indent || E.cy >= E.numrows) {
        editorInsertNewline();
        return;
    }

    erow *row = &E.row[E.cy];
    int prefix_length = 0;
    while (prefix_length < E.cx && prefix_length < row->size &&
           (row->chars[prefix_length] == ' ' ||
            row->chars[prefix_length] == '\t'))
        prefix_length++;

    char opener = E.cx > 0 ? row->chars[E.cx - 1] : '\0';
    char closer = closingBracket(opener);
    int extra = (closer || opener == ':') ? E.tab_width : 0;
    int paired = closer && E.cx < row->size && row->chars[E.cx] == closer;
    int length = 2 + prefix_length + extra;
    if (paired) length += 2 + prefix_length;

    char *insertion = malloc(length + 1);
    if (!insertion) die("malloc: automatic indentation");
    char *at = insertion;
    *at++ = '\r';
    *at++ = '\n';
    if (prefix_length > 0) {
        memcpy(at, row->chars, prefix_length);
        at += prefix_length;
    }
    memset(at, ' ', extra);
    at += extra;
    if (paired) {
        *at++ = '\r';
        *at++ = '\n';
        if (prefix_length > 0) {
            memcpy(at, row->chars, prefix_length);
            at += prefix_length;
        }
    }
    *at = '\0';

    int result_row = paired ? E.cy + 1 : -1;
    int result_col = paired ? prefix_length + extra : -1;
    editorInsertAtomicText(insertion, length, result_row, result_col);
    free(insertion);
}

int editorDeleteAutoPair(void) {
    if (!E.auto_pair || E.cy >= E.numrows || E.cx <= 0) return 0;
    erow *row = &E.row[E.cy];
    if (E.cx >= row->size) return 0;
    char opener = row->chars[E.cx - 1];
    char closer = closingBracket(opener);
    if (!closer || row->chars[E.cx] != closer) return 0;

    int row_index = E.cy;
    int cursor_col = E.cx;
    char *pair = malloc(3);
    if (!pair) die("malloc: deleted pair");
    pair[0] = opener;
    pair[1] = closer;
    pair[2] = '\0';

    undo_suspended++;
    editorRowDelChar(row, E.cx);
    editorRowDelChar(row, E.cx - 1);
    if (undo_suspended > 0) undo_suspended--;
    E.cx--;
    editorPushUndoBlock(UO_ATOMIC_DELETE,
                        row_index, cursor_col - 1,
                        row_index, cursor_col + 1,
                        row_index, cursor_col, pair, 2);
    return 1;
}

void editorDelChar(void) {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        int prev = editorPrevCharByte(row, E.cx);
        editorRowDelChar(row, prev);
        undo_stack[undo_count - 1].cursor_row = E.cy;
        undo_stack[undo_count - 1].cursor_col = E.cx;
        E.cx = prev;
    } else {
        if (!undo_suspended) {
            int saved_size = E.row[E.cy - 1].size;
            editorPushUndo(UO_JOIN_ROW, E.cy - 1, saved_size,
                           E.cy, E.cx, NULL, 0);
        }
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

/* -- insert a block of text at cursor (paste / redo helper) -----
   Always suppress inner char/row undo entries.  The caller records
   the whole operation as one block-level undo record.             */
void editorInsertBlock(char *text, int len) {
    int saved_suspend = undo_suspended;
    undo_suspended++;

    int i = 0;
    while (i < len) {
        if (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n')
            { editorInsertNewline(); i += 2; }
        else if (text[i] == '\n')
            { editorInsertNewline(); i++; }
        else if (text[i] == '\r')
            { editorInsertNewline(); i++; }
        else
            { editorInsertChar((unsigned char)text[i]); i++; }
    }

    undo_suspended = saved_suspend;
}
