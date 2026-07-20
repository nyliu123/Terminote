#include "common.h"
#include "row.h"
#include "undo.h"
#include "output.h"
#include "editor.h"
#include "mouse.h"
#include "localization.h"

static int editorMouseTextWidth(void) {
    int width = E.screencols - (E.linenum_width + 1);
    return width < 1 ? 1 : width;
}

static int editorScrollPercent(int offset, int maximum) {
    if (maximum <= 0) return 100;
    if (offset <= 0) return 0;
    if (offset >= maximum) return 100;
    return (int)((long long)offset * 100 / maximum);
}

static void editorSetVerticalScrollEvent(void) {
    int maximum = E.numrows - editorVisibleRows();
    if (maximum < 0) maximum = 0;
    if (maximum == 0) editorSetEvent("%s", L("editor.ready"));
    else editorSetEvent(L("mouse.vertical_scroll"),
                        editorScrollPercent(E.rowoff, maximum));
}

static void editorSetHorizontalScrollEvent(int maximum) {
    if (maximum <= 0) editorSetEvent("%s", L("editor.ready"));
    else editorSetEvent(L("mouse.horizontal_scroll"),
                        editorScrollPercent(E.coloff, maximum));
}

static void editorScrollbarMetrics(int text_width, int *thumb_width,
                                   int *max_offset) {
    int max_render = editorMaxRenderWidth(text_width);
    double ratio = (double)text_width / max_render;
    *thumb_width = (int)(text_width * ratio);
    if (*thumb_width < 2) *thumb_width = 2;
    if (*thumb_width > text_width) *thumb_width = text_width;
    *max_offset = max_render - text_width + 1;
}

static void editorPressScrollbar(int show_event) {
    int text_width = editorMouseTextWidth();
    int x = E.mouse_x - (E.linenum_width + 1);
    if (x < 0) x = 0;
    if (x >= text_width) x = text_width - 1;

    int thumb_width, max_offset;
    editorScrollbarMetrics(text_width, &thumb_width, &max_offset);
    if (max_offset > 0) {
        int track_width = text_width - thumb_width;
        if (track_width <= 0) track_width = 1;
        int offset = (int)((double)(x - thumb_width / 2) /
                           track_width * max_offset);
        if (offset < 0) offset = 0;
        if (offset > max_offset) offset = max_offset;
        E.coloff = offset;
    }
    if (show_event) editorSetHorizontalScrollEvent(max_offset);

    E.hscroll_dragging = 1;
    E.wheel_scrolling = 1;
    E.hscroll_start_coloff = E.coloff;
    E.hscroll_start_mx = E.mouse_x;
}

static void editorMouseDocumentPosition(int *row, int *col) {
    int file_row = E.mouse_y - 1 + E.rowoff;
    if (file_row >= E.numrows) file_row = E.numrows > 0 ? E.numrows - 1 : 0;
    if (file_row < 0) file_row = 0;
    int x = E.mouse_x - (E.linenum_width + 1);
    if (x < 0) x = 0;
    *row = file_row;
    *col = file_row < E.numrows
        ? editorRowRxToCx(&E.row[file_row], x + E.coloff) : 0;
}

static void editorBeginSelectionOrMove(void) {
    int row, col;
    editorMouseDocumentPosition(&row, &col);

    if (editorPosInSelection(row, col)) {
        int length;
        free(E.drag_text);
        E.drag_text = editorGetSelectedText(&length);
        E.drag_text_len = E.drag_text && length > 0 ? length : 0;

        int start_row = E.sel_anchor_row, start_col = E.sel_anchor_col;
        int end_row = E.cy, end_col = E.cx;
        if (end_row < start_row ||
            (end_row == start_row && end_col < start_col)) {
            int swap = start_row; start_row = end_row; end_row = swap;
            swap = start_col; start_col = end_col; end_col = swap;
        }
        E.drag_sel_sr = start_row; E.drag_sel_sc = start_col;
        E.drag_sel_er = end_row; E.drag_sel_ec = end_col;
        E.sel_active = 0;
        E.mouse_drag_move = 1;
    } else {
        editorClearSelection();
        E.sel_anchor_row = row;
        E.sel_anchor_col = col;
        E.mouse_selecting = 1;
    }
    E.cy = row;
    E.cx = col;
}

static void editorHandleMousePress(void) {
    E.wheel_scrolling = 0;
    if (E.mouse_y == E.screenrows && editorVisibleRows() < E.screenrows)
        editorPressScrollbar(1);
    else
        editorBeginSelectionOrMove();
}

static void editorDragScrollbar(int show_event) {
    int text_width = editorMouseTextWidth();
    int thumb_width, max_offset;
    editorScrollbarMetrics(text_width, &thumb_width, &max_offset);
    if (max_offset <= 0) return;

    int track_width = text_width - thumb_width;
    if (track_width <= 0) track_width = 1;
    int delta = E.mouse_x - E.hscroll_start_mx;
    int offset = E.hscroll_start_coloff +
                 (int)((double)delta / track_width * max_offset);
    if (offset < 0) offset = 0;
    if (offset > max_offset) offset = max_offset;
    E.coloff = offset;
    if (show_event) editorSetHorizontalScrollEvent(max_offset);
}

int editorHandleModalViewInput(int key, int allow_last_wheel_step) {
    switch (key) {
        case ACTION_PAGE_UP:
        case ACTION_PAGE_DOWN: {
            int rows = editorVisibleRows();
            if (rows < 1) rows = 1;
            E.wheel_scrolling = 1;
            E.cy = key == ACTION_PAGE_UP ? E.rowoff : E.rowoff + rows - 1;
            if (E.cy >= E.numrows && E.numrows > 0) E.cy = E.numrows - 1;
            for (int i = 0; i < rows; i++)
                editorMoveCursor(key == ACTION_PAGE_UP
                                 ? ACTION_MOVE_UP : ACTION_MOVE_DOWN);
            return 1;
        }
        case WHEEL_UP:
            if (E.rowoff > 0) { E.rowoff--; E.wheel_scrolling = 1; }
            return 1;
        case WHEEL_DOWN:
            if (E.rowoff + E.screenrows < E.numrows ||
                (allow_last_wheel_step &&
                 E.rowoff + E.screenrows == E.numrows)) {
                E.rowoff++;
                E.wheel_scrolling = 1;
            }
            return 1;
        case MOUSE_PRESS:
            if (E.mouse_y != E.screenrows ||
                editorVisibleRows() >= E.screenrows) return 0;
            editorPressScrollbar(0);
            return 1;
        case MOUSE_DRAG:
            if (E.hscroll_dragging) editorDragScrollbar(0);
            return 1;
        case MOUSE_RELEASE:
            E.hscroll_dragging = 0;
            return 1;
        default:
            return 0;
    }
}

static void editorUpdateDragPosition(void) {
    E.wheel_scrolling = 0;
    int text_width = editorMouseTextWidth();
    int content_rows = editorVisibleRows();
    if (content_rows < 1) content_rows = 1;

    if (E.mouse_y <= 1 && E.rowoff > 0) E.auto_scroll_v = -1;
    else if (E.mouse_y >= content_rows &&
             E.rowoff + content_rows < E.numrows) E.auto_scroll_v = 1;
    else E.auto_scroll_v = 0;

    int x = E.mouse_x - (E.linenum_width + 1);
    if (x <= 0 && E.coloff > 0) E.auto_scroll_h = -1;
    else if (x >= text_width - 1) E.auto_scroll_h = 1;
    else E.auto_scroll_h = 0;

    if (E.auto_scroll_v < 0) E.rowoff--;
    if (E.auto_scroll_v > 0) E.rowoff++;
    if (E.auto_scroll_h < 0) E.coloff--;
    if (E.auto_scroll_h > 0) E.coloff++;

    int row = E.mouse_y - 1 + E.rowoff;
    if (row >= E.numrows) row = E.numrows > 0 ? E.numrows - 1 : 0;
    if (row < 0) row = 0;
    if (x < 0) x = 0;
    if (x >= text_width) x = text_width - 1;
    E.cy = row;
    E.cx = row < E.numrows
        ? editorRowRxToCx(&E.row[row], x + E.coloff) : 0;
    if (E.mouse_selecting) E.sel_active = 1;
}

static int editorDropInsideSelection(int row, int col,
                                     int sr, int sc, int er, int ec) {
    return (row > sr && row < er) ||
           (row == sr && row == er && col >= sc && col <= ec) ||
           (row == sr && row != er && col >= sc) ||
           (row == er && row != sr && col <= ec);
}

static void editorAdjustDropPosition(int drop_row, int drop_col,
                                     int sr, int sc, int er, int ec,
                                     int *row, int *col) {
    *row = drop_row;
    *col = drop_col;
    if (drop_row > er) {
        *row -= er - sr;
    } else if (drop_row == er) {
        if (sr == er) {
            if (drop_col > ec) *col -= ec - sc;
            else if (drop_col > sc) *col = sc;
        } else {
            *row = sr;
            *col = drop_col >= ec ? sc + (drop_col - ec) : sc;
        }
    }
}

static void editorCancelDragText(void) {
    free(E.drag_text);
    E.drag_text = NULL;
    E.drag_text_len = 0;
}

static void editorFinishTextMove(void) {
    E.mouse_drag_move = 0;
    E.auto_scroll_h = 0;
    E.auto_scroll_v = 0;
    if (!E.drag_text || E.drag_text_len == 0) return;

    int drop_row = E.cy, drop_col = E.cx;
    int sr = E.drag_sel_sr, sc = E.drag_sel_sc;
    int er = E.drag_sel_er, ec = E.drag_sel_ec;
    if (editorDropInsideSelection(drop_row, drop_col, sr, sc, er, ec)) {
        editorCancelDragText();
        return;
    }

    int target_row, target_col;
    editorAdjustDropPosition(drop_row, drop_col, sr, sc, er, ec,
                             &target_row, &target_col);
    if (target_row == sr && target_col == sc) {
        editorCancelDragText();
        return;
    }

    char *text = E.drag_text;
    int length = E.drag_text_len;
    E.drag_text = NULL;
    E.drag_text_len = 0;
    undo_suspended++;
    editorDeleteRange(sr, sc, er, ec);

    if (target_row >= E.numrows) target_row = E.numrows > 0 ? E.numrows - 1 : 0;
    if (target_row < 0) target_row = 0;
    if (target_col > E.row[target_row].size) target_col = E.row[target_row].size;
    E.cy = target_row;
    E.cx = target_col;
    editorInsertBlock(text, length);
    if (undo_suspended > 0) undo_suspended--;

    editorPushUndoMoveBlock(sr, sc, er, ec,
                            target_row, target_col,
                            sr, sc, text, length);
}

static void editorHandleMouseRelease(void) {
    if (E.mouse_drag_move) {
        editorFinishTextMove();
        editorSetEvent("%s", L("editor.content_drag"));
        return;
    }
    if (E.hscroll_dragging) {
        int thumb_width, max_offset;
        editorScrollbarMetrics(editorMouseTextWidth(),
                               &thumb_width, &max_offset);
        editorSetHorizontalScrollEvent(max_offset);
    }
    E.mouse_selecting = 0;
    E.hscroll_dragging = 0;
    E.auto_scroll_h = 0;
    E.auto_scroll_v = 0;
}

void editorProcessMouseEvent(int event) {
    switch (event) {
        case WHEEL_UP:
            if (E.rowoff > 0) {
                E.rowoff--;
                E.wheel_scrolling = 1;
            }
            editorSetVerticalScrollEvent();
            break;
        case WHEEL_DOWN:
            if (E.rowoff + E.screenrows <= E.numrows) {
                E.rowoff++;
                E.wheel_scrolling = 1;
            }
            editorSetVerticalScrollEvent();
            break;
        case MOUSE_PRESS:
            editorHandleMousePress();
            break;
        case MOUSE_DRAG:
            if (E.hscroll_dragging) editorDragScrollbar(1);
            else if (E.mouse_selecting || E.mouse_drag_move)
                editorUpdateDragPosition();
            break;
        case MOUSE_RELEASE:
            editorHandleMouseRelease();
            break;
        case MOUSE_CLICK:
        default:
            break;
    }
}
