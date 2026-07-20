#include "common.h"
#include "row.h"
#include "editor.h"
#include "output.h"

/* ---- cursor movement ---- */
void editorMoveCursor(int key) {
    E.wheel_scrolling = 0;
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key) {
        case ACTION_MOVE_LEFT:
            if (E.cx != 0)
                E.cx = editorPrevCharByte(row, E.cx);
            else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ACTION_MOVE_RIGHT:
            if (row && E.cx < row->size)
                E.cx = editorNextCharByte(row, E.cx);
            else if (row && E.cx == row->size && E.cy < E.numrows - 1) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ACTION_MOVE_UP:
            if (E.cy != 0) E.cy--;
            break;
        case ACTION_MOVE_DOWN:
            if (E.cy < E.numrows - 1) E.cy++;
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) E.cx = rowlen;
}

/* ---- scroll ---- */
void editorScroll(void) {
    E.rx = 0;
    if (E.cy < E.numrows)
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);

    int gutter_w  = E.linenum_width + 1;
    int text_cols = E.screencols - gutter_w;
    if (text_cols < 1) text_cols = 1;

    if (!E.wheel_scrolling) {
        int vis_rows = editorVisibleRows();
        if (vis_rows < 1) vis_rows = 1;
        if (E.cy < E.rowoff)
            E.rowoff = E.cy;
        if (E.cy >= E.rowoff + vis_rows)
            E.rowoff = E.cy - vis_rows + 1;

        if (E.rx < E.coloff)
            E.coloff = E.rx;
        if (E.rx >= E.coloff + text_cols)
            E.coloff = E.rx - text_cols + 1;
    }
}
