#include "common.h"
#include "row.h"
#include "undo.h"
#include "output.h"
#include "editor.h"
#include "localization.h"

/* ---- selection ---- */
void editorClearSelection(void) {
    E.sel_active = 0;
}

/* ---- return 1 if (row, col) falls inside the current selection ---- */
int editorPosInSelection(int row, int col) {
    if (!E.sel_active || E.numrows == 0) return 0;

    int sr = E.sel_anchor_row, sc = E.sel_anchor_col;
    int er = E.cy,             ec = E.cx;
    if (er < sr || (er == sr && ec < sc)) {
        int t = sr; sr = er; er = t;
        t = sc; sc = ec; ec = t;
    }
    if (sr >= E.numrows) sr = E.numrows - 1;
    if (er >= E.numrows) er = E.numrows - 1;
    if (sc > E.row[sr].size) sc = E.row[sr].size;
    if (ec > E.row[er].size) ec = E.row[er].size;
    if (sr == er) return (row == sr && col >= sc && col < ec);
    if (row > sr && row < er) return 1;
    if (row == sr) return col >= sc;
    if (row == er) return col < ec;
    return 0;
}

void editorStartSelection(void) {
    if (E.sel_active) return;
    if (E.cy >= E.numrows && E.numrows > 0) {
        E.sel_anchor_row = E.numrows - 1;
        E.sel_anchor_col = E.row[E.numrows - 1].size;
    } else if (E.cy < E.numrows) {
        E.sel_anchor_row = E.cy;
        E.sel_anchor_col = E.cx;
    }
    E.sel_active = 1;
}

char *editorGetSelectedText(int *outlen) {
    if (!E.sel_active) { *outlen = 0; return NULL; }

    int start_row = E.sel_anchor_row, start_col = E.sel_anchor_col;
    int end_row   = E.cy,             end_col   = E.cx;

    if (end_row < start_row || (end_row == start_row && end_col < start_col)) {
        int t = start_row; start_row = end_row; end_row = t;
        t = start_col; start_col = end_col; end_col = t;
    }

    if (start_row >= E.numrows) { *outlen = 0; return NULL; }
    if (end_row   >= E.numrows)   end_row = E.numrows - 1;
    if (start_col > E.row[start_row].size) start_col = E.row[start_row].size;
    if (end_col   > E.row[end_row].size)   end_col   = E.row[end_row].size;

    int totlen;
    if (start_row == end_row) {
        totlen = end_col - start_col;
    } else {
        totlen  = E.row[start_row].size - start_col + 2;
        for (int i = start_row + 1; i < end_row; i++)
            totlen += E.row[i].size + 2;
        totlen += end_col;
    }

    char *buf = malloc(totlen + 1);
    if (!buf) die("malloc: editorGetSelectedText");
    char *p = buf;

    if (start_row == end_row) {
        memcpy(p, &E.row[start_row].chars[start_col], end_col - start_col);
        p += end_col - start_col;
    } else {
        erow *r = &E.row[start_row];
        memcpy(p, &r->chars[start_col], r->size - start_col);
        p += r->size - start_col;
        *p++ = '\r'; *p++ = '\n';
        for (int i = start_row + 1; i < end_row; i++) {
            memcpy(p, E.row[i].chars, E.row[i].size);
            p += E.row[i].size;
            *p++ = '\r'; *p++ = '\n';
        }
        memcpy(p, E.row[end_row].chars, end_col);
        p += end_col;
    }
    *p = '\0';
    *outlen = totlen;
    return buf;
}

/* -- delete selection (shared by cut and Backspace/Delete) ----
   records the entire deletion as a single undo step.              */
void editorDeleteSelection(void) {
    if (!E.sel_active) return;

    int start_row = E.sel_anchor_row, start_col = E.sel_anchor_col;
    int end_row   = E.cy,             end_col   = E.cx;

    if (end_row < start_row || (end_row == start_row && end_col < start_col)) {
        int t = start_row; start_row = end_row; end_row = t;
        t = start_col; start_col = end_col; end_col = t;
    }

    if (start_row >= E.numrows) return;
    if (end_row   >= E.numrows) end_row = E.numrows - 1;
    if (start_col > E.row[start_row].size) start_col = E.row[start_row].size;
    if (end_col   > E.row[end_row].size)   end_col   = E.row[end_row].size;

    /* capture the text we are about to delete */
    int del_len;
    char *del_text = editorGetTextInRange(start_row, start_col,
                                          end_row, end_col, &del_len);

    editorClearSelection();
    undo_suspended++;

    if (start_row == end_row) {
        erow *row = &E.row[start_row];
        int count = end_col - start_col;
        for (int i = 0; i < count; i++)
            editorRowDelChar(row, start_col);
        E.cy = start_row;
        E.cx = start_col;
    } else {
        erow *startr = &E.row[start_row];
        erow *endr   = &E.row[end_row];

        int tail_len = endr->size - end_col;
        char *tail = NULL;
        if (tail_len > 0) {
            tail = malloc(tail_len);
            if (tail) memcpy(tail, &endr->chars[end_col], tail_len);
        }

        for (int i = end_row; i > start_row; i--)
            editorDelRow(i);

        startr->size = start_col;
        startr->chars[start_col] = '\0';

        if (tail && tail_len > 0) {
            editorRowAppendString(startr, tail, tail_len);
            free(tail);
        }

        editorUpdateRow(startr);
        E.cy = start_row;
        E.cx = start_col;
    }

    editorUpdateLineNumWidth();
    if (undo_suspended > 0) undo_suspended--;
    E.dirty++;

    /* one undo step for the whole deletion --skip if caller
       has suspended undo (e.g. paste-over-selection) */
    if (del_text && !undo_suspended) {
        editorPushUndoBlock(UO_ATOMIC_DELETE, start_row, start_col,
                            end_row, end_col,
                            start_row, start_col, del_text, del_len);
    } else {
        free(del_text);
    }
}

/* ---- get text from an explicit range (for undo block support) ---- */
char *editorGetTextInRange(int sr, int sc, int er, int ec, int *outlen) {
    if (sr >= E.numrows) { *outlen = 0; return NULL; }
    if (er >= E.numrows)   er = E.numrows - 1;
    if (sc > E.row[sr].size) sc = E.row[sr].size;
    if (ec > E.row[er].size) ec = E.row[er].size;

    int totlen;
    if (sr == er) {
        totlen = ec - sc;
    } else {
        totlen  = E.row[sr].size - sc + 2;
        for (int i = sr + 1; i < er; i++)
            totlen += E.row[i].size + 2;
        totlen += ec;
    }

    char *buf = malloc(totlen + 1);
    if (!buf) die("malloc: editorGetTextInRange");
    char *p = buf;

    if (sr == er) {
        memcpy(p, &E.row[sr].chars[sc], ec - sc);
        p += ec - sc;
    } else {
        erow *r = &E.row[sr];
        memcpy(p, &r->chars[sc], r->size - sc);
        p += r->size - sc;
        *p++ = '\r'; *p++ = '\n';
        for (int i = sr + 1; i < er; i++) {
            memcpy(p, E.row[i].chars, E.row[i].size);
            p += E.row[i].size;
            *p++ = '\r'; *p++ = '\n';
        }
        memcpy(p, E.row[er].chars, ec);
        p += ec;
    }
    *p = '\0';
    *outlen = totlen;
    return buf;
}

/* ---- delete an explicit range (for undo block support) ---- */
void editorDeleteRange(int sr, int sc, int er, int ec) {
    if (sr >= E.numrows) return;
    if (er >= E.numrows) er = E.numrows - 1;
    if (sc > E.row[sr].size) sc = E.row[sr].size;
    if (ec > E.row[er].size) ec = E.row[er].size;

    if (sr == er) {
        erow *row = &E.row[sr];
        int count = ec - sc;
        for (int i = 0; i < count; i++)
            editorRowDelChar(row, sc);
        E.cy = sr;
        E.cx = sc;
    } else {
        erow *startr = &E.row[sr];
        erow *endr   = &E.row[er];

        int tail_len = endr->size - ec;
        char *tail = NULL;
        if (tail_len > 0) {
            tail = malloc(tail_len);
            if (tail) memcpy(tail, &endr->chars[ec], tail_len);
        }

        for (int i = er; i > sr; i--)
            editorDelRow(i);

        startr->size = sc;
        startr->chars[sc] = '\0';

        if (tail && tail_len > 0) {
            editorRowAppendString(startr, tail, tail_len);
            free(tail);
        }

        editorUpdateRow(startr);
        E.cy = sr;
        E.cx = sc;
    }

    editorUpdateLineNumWidth();
    E.dirty++;
}

/* ---- cut selection ---- */
void editorCutSelection(void) {
    if (!E.sel_active) return;

    int len;
    char *sel = editorGetSelectedText(&len);
    if (sel && len > 0) {
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
            if (hMem) {
                char *p = (char *)GlobalLock(hMem);
                memcpy(p, sel, len);
                p[len] = '\0';
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
            CloseClipboard();
        }
        free(sel);
        editorSetStatusMessage("%s", L("selection.cut"));
    } else {
        free(sel);
        return;
    }

    editorDeleteSelection();
}

/* -- indent selection ------------------------------------------
   Indents only the selected fragments: the first row begins at the
   selection column, while subsequent selected rows begin at zero. */
void editorIndentSelection(void) {
    if (!E.sel_active || E.numrows == 0) return;

    int sr = E.sel_anchor_row, sc = E.sel_anchor_col;
    int er = E.cy,             ec = E.cx;
    int selection_forward = 1;
    if (er < sr || (er == sr && ec < sc)) {
        selection_forward = 0;
        int t = sr; sr = er; er = t;
        t = sc; sc = ec; ec = t;
    }
    int modified_er = er;
    if (modified_er > sr && ec == 0) modified_er--;
    if (modified_er < sr) return;

    int saved_cy = E.cy, saved_cx = E.cx;
    int n = E.tab_width;

    int old_len;
    char *old_text = editorGetTextInRange(
        sr, 0, modified_er, E.row[modified_er].size, &old_len);

    undo_suspended++;
    for (int i = sr; i <= modified_er; i++) {
        int insert_col = i == sr ? sc : 0;
        for (int j = 0; j < n; j++)
            editorRowInsertChar(&E.row[i], insert_col + j, ' ');
    }
    if (undo_suspended > 0) undo_suspended--;

    if (modified_er == er) {
        if (selection_forward) E.cx += n;
        else E.sel_anchor_col += n;
    }

    editorUpdateLineNumWidth();

    if (old_text) {
        editorPushUndoBlock(UO_INDENT_LINES, sr, 0, modified_er,
                            E.row[modified_er].size,
                            saved_cy, saved_cx,
                            old_text, old_len);
    }

    editorSetEvent(L("edit.indent"), n);
}

/* -- unindent selection ----------------------------------------
   Removes indentation only at each selected fragment's beginning. */
void editorUnindentSelection(void) {
    if (!E.sel_active || E.numrows == 0) return;

    int sr = E.sel_anchor_row, sc = E.sel_anchor_col;
    int er = E.cy,             ec = E.cx;
    int selection_forward = 1;
    if (er < sr || (er == sr && ec < sc)) {
        selection_forward = 0;
        int t = sr; sr = er; er = t;
        t = sc; sc = ec; ec = t;
    }
    int modified_er = er;
    if (modified_er > sr && ec == 0) modified_er--;
    if (modified_er < sr) return;

    int saved_cy = E.cy, saved_cx = E.cx;
    int n = E.tab_width;

    int old_len;
    char *old_text = editorGetTextInRange(
        sr, 0, modified_er, E.row[modified_er].size, &old_len);

    undo_suspended++;
    int end_removed = 0;
    for (int i = sr; i <= modified_er; i++) {
        erow *row = &E.row[i];
        int remove_col = i == sr ? sc : 0;
        int spaces = 0;
        while (spaces < n && remove_col + spaces < row->size &&
               row->chars[remove_col + spaces] == ' ')
            spaces++;
        for (int j = 0; j < spaces; j++)
            editorRowDelChar(row, remove_col);
        if (i == er) end_removed = spaces;
    }
    if (undo_suspended > 0) undo_suspended--;

    if (modified_er == er) {
        if (selection_forward) {
            E.cx -= end_removed;
            if (E.cx < 0) E.cx = 0;
        } else {
            E.sel_anchor_col -= end_removed;
            if (E.sel_anchor_col < 0) E.sel_anchor_col = 0;
        }
    }

    editorUpdateLineNumWidth();

    if (old_text) {
        editorPushUndoBlock(UO_UNINDENT_LINES, sr, 0, modified_er,
                            E.row[modified_er].size,
                            saved_cy, saved_cx,
                            old_text, old_len);
    }

    editorSetEvent("%s", L("edit.unindent"));
}
