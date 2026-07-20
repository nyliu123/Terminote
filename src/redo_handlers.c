#include "common.h"
#include "row.h"
#include "editor.h"
#include "undo.h"
#include "undo_internal.h"
#include "redo_internal.h"

typedef void (*redoHandler)(struct undoRecord *, int);

static void restoreRecordCursor(const struct undoRecord *record) {
    E.cy = record->cursor_row;
    E.cx = record->cursor_col;
}

static void redoInsertChar(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    erow *row = &E.row[record->row];
    char saved[4] = {0};
    int length = 0;
    if (record->col < row->size) {
        length = utf8_seq_len((unsigned char)row->chars[record->col]);
        if (record->col + length > row->size) length = row->size - record->col;
        memcpy(saved, &row->chars[record->col], length);
    }
    editorRowDelChar(row, record->col);
    restoreRecordCursor(record);
    editorPushUndo(inverse, record->row, record->col, cy, cx, saved, length);
}

static void redoDeleteChar(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    erow *row = &E.row[record->row];
    for (int i = 0; i < record->save_len; i++)
        editorRowInsertChar(row, record->col + i,
                            (unsigned char)record->save[i]);
    restoreRecordCursor(record);
    editorPushUndo(inverse, record->row, record->col, cy, cx, NULL, 0);
}

static void redoSplitRow(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    E.cx = E.row[record->row].size;
    editorRowAppendString(&E.row[record->row], E.row[record->row + 1].chars,
                          E.row[record->row + 1].size);
    editorDelRow(record->row + 1);
    restoreRecordCursor(record);
    editorPushUndo(inverse, record->row, record->col, cy, cx, NULL, 0);
}

static void redoJoinRow(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    erow *row = &E.row[record->row];
    editorInsertRow(record->row + 1, &row->chars[record->col],
                    row->size - record->col);
    row = &E.row[record->row];
    row->size = record->col;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    restoreRecordCursor(record);
    editorPushUndo(inverse, record->row, record->col, cy, cx, NULL, 0);
}

static void redoInsertRow(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    if (E.numrows > 1) editorDelRow(record->row);
    else {
        erow *row = &E.row[record->row];
        row->size = 0;
        row->chars[0] = '\0';
        editorUpdateRow(row);
    }
    restoreRecordCursor(record);
    editorPushUndo(inverse, record->row, record->col, cy, cx, NULL, 0);
}

static void redoDeleteRow(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    editorInsertRow(record->row, "", 0);
    restoreRecordCursor(record);
    editorPushUndo(inverse, record->row, 0, cy, cx, NULL, 0);
}

static void redoInsertBlock(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    char *text = record->block_text;
    int length = record->block_len;
    if (text) record->block_text = NULL;
    else text = editorGetTextInRange(record->row, record->col,
                                     record->end_row, record->end_col,
                                     &length);
    editorDeleteRange(record->row, record->col,
                      record->end_row, record->end_col);
    restoreRecordCursor(record);
    editorPushUndoBlock(inverse, record->row, record->col,
                        record->end_row, record->end_col,
                        cy, cx, text, length);
}

static void redoDeleteBlock(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    E.cy = record->row;
    E.cx = record->col;
    editorInsertBlock(record->block_text, record->block_len);
    restoreRecordCursor(record);
    editorPushUndoBlock(inverse, record->row, record->col,
                        record->end_row, record->end_col,
                        cy, cx, record->block_text, record->block_len);
    record->block_text = NULL;
}

static void redoAtomicInsert(struct undoRecord *record, int inverse) {
    (void)inverse;
    E.cy = record->row;
    E.cx = record->col;
    editorInsertBlock(record->block_text, record->block_len);
    E.cy = record->result_row >= 0 ? record->result_row : record->end_row;
    E.cx = record->result_row >= 0 ? record->result_col : record->end_col;
    editorMoveRecordToUndo(record);
}

static void redoAtomicDelete(struct undoRecord *record, int inverse) {
    (void)inverse;
    editorDeleteRange(record->row, record->col,
                      record->end_row, record->end_col);
    E.cy = record->row;
    E.cx = record->col;
    editorMoveRecordToUndo(record);
}

static void redoAtomicReplace(struct undoRecord *record, int inverse) {
    (void)inverse;
    editorDeleteRange(record->row, record->col,
                      record->end_row, record->end_col);
    E.cy = record->row;
    E.cx = record->col;
    editorInsertBlock(record->new_text, record->new_len);
    E.cy = record->result_row >= 0 ? record->result_row : record->new_end_row;
    E.cx = record->result_row >= 0 ? record->result_col : record->new_end_col;
    editorMoveRecordToUndo(record);
}

static void redoIndent(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy, length;
    char *text = editorGetTextInRange(
        record->row, 0, record->end_row, E.row[record->end_row].size, &length);
    editorDeleteRange(record->row, 0, record->end_row,
                      E.row[record->end_row].size);
    E.cy = record->row;
    E.cx = 0;
    editorInsertBlock(record->block_text, record->block_len);
    editorPushUndoBlock(inverse, record->row, 0,
                        record->end_row, E.row[record->end_row].size,
                        cy, cx, text, length);
    restoreRecordCursor(record);
}

static void redoReplaceBlock(struct undoRecord *record, int inverse) {
    (void)inverse;
    int cx = E.cx, cy = E.cy, length;
    char *text = editorGetTextInRange(record->row, record->col,
                                      record->end_row, record->end_col,
                                      &length);
    editorDeleteRange(record->row, record->col,
                      record->end_row, record->end_col);
    E.cy = record->row;
    E.cx = record->col;
    editorInsertBlock(record->block_text, record->block_len);
    int end_row, end_col;
    editorBlockEndPosition(record->row, record->col,
                           record->block_text, record->block_len,
                           &end_row, &end_col);
    editorPushUndoBlock(UO_REPLACE_BLOCK, record->row, record->col,
                        end_row, end_col, cy, cx, text, length);
    restoreRecordCursor(record);
}

static void redoMoveBlock(struct undoRecord *record, int inverse) {
    (void)inverse;
    if (!record->block_text || record->block_len <= 0) return;
    editorDeleteRange(record->row, record->col,
                      record->end_row, record->end_col);
    E.cy = record->dest_row;
    E.cx = record->dest_col;
    editorInsertBlock(record->block_text, record->block_len);
    int end_row, end_col;
    editorBlockEndPosition(record->dest_row, record->dest_col,
                           record->block_text, record->block_len,
                           &end_row, &end_col);
    editorMoveRecordToUndo(record);
    editorClearSelection();
    E.cy = end_row;
    E.cx = end_col;
}

void editorReplayRedo(struct undoRecord *record, int inverse) {
    static redoHandler handlers[UO_ATOMIC_MOVE + 1] = {
        [UO_INSERT_CHAR] = redoInsertChar,
        [UO_DELETE_CHAR] = redoDeleteChar,
        [UO_SPLIT_ROW] = redoSplitRow,
        [UO_JOIN_ROW] = redoJoinRow,
        [UO_INSERT_ROW] = redoInsertRow,
        [UO_DELETE_ROW] = redoDeleteRow,
        [UO_INSERT_BLOCK] = redoInsertBlock,
        [UO_DELETE_BLOCK] = redoDeleteBlock,
        [UO_INDENT_LINES] = redoIndent,
        [UO_UNINDENT_LINES] = redoIndent,
        [UO_REPLACE_BLOCK] = redoReplaceBlock,
        [UO_MOVE_BLOCK] = redoMoveBlock,
        [UO_ATOMIC_INSERT] = redoAtomicInsert,
        [UO_ATOMIC_DELETE] = redoAtomicDelete,
        [UO_ATOMIC_REPLACE] = redoAtomicReplace,
        [UO_ATOMIC_MOVE] = redoMoveBlock
    };
    if (record->op_type >= UO_INSERT_CHAR &&
        record->op_type <= UO_ATOMIC_MOVE && handlers[record->op_type])
        handlers[record->op_type](record, inverse);
}
