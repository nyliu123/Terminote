#include "common.h"
#include "row.h"
#include "editor.h"
#include "undo.h"
#include "undo_internal.h"

typedef void (*undoHandler)(struct undoRecord *, int);

static void restoreCursor(const struct undoRecord *record) {
    E.cy = record->cursor_row;
    E.cx = record->cursor_col;
}

static void undoInsertChar(struct undoRecord *record, int inverse) {
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
    restoreCursor(record);
    editorPushRedo(inverse, record->row, record->col, cy, cx, saved, length);
}

static void undoDeleteChar(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    erow *row = &E.row[record->row];
    for (int i = 0; i < record->save_len; i++)
        editorRowInsertChar(row, record->col + i,
                            (unsigned char)record->save[i]);
    restoreCursor(record);
    editorPushRedo(inverse, record->row, record->col, cy, cx, NULL, 0);
}

static void undoSplitRow(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    E.cx = E.row[record->row].size;
    editorRowAppendString(&E.row[record->row], E.row[record->row + 1].chars,
                          E.row[record->row + 1].size);
    editorDelRow(record->row + 1);
    restoreCursor(record);
    editorPushRedo(inverse, record->row, record->col, cy, cx, NULL, 0);
}

static void undoJoinRow(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    erow *row = &E.row[record->row];
    editorInsertRow(record->row + 1, &row->chars[record->col],
                    row->size - record->col);
    row = &E.row[record->row];
    row->size = record->col;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    restoreCursor(record);
    editorPushRedo(inverse, record->row, record->col, cy, cx, NULL, 0);
}

static void undoInsertRow(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    if (E.numrows > 1) editorDelRow(record->row);
    else {
        erow *row = &E.row[record->row];
        row->size = 0;
        row->chars[0] = '\0';
        editorUpdateRow(row);
    }
    restoreCursor(record);
    editorPushRedo(inverse, record->row, record->col, cy, cx, NULL, 0);
}

static void undoDeleteRow(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    editorInsertRow(record->row, "", 0);
    restoreCursor(record);
    editorPushRedo(inverse, record->row, 0, cy, cx, NULL, 0);
}

static void undoInsertBlock(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    char *text = record->block_text;
    int length = record->block_len;
    if (text) record->block_text = NULL;
    else text = editorGetTextInRange(record->row, record->col,
                                     record->end_row, record->end_col,
                                     &length);
    editorDeleteRange(record->row, record->col,
                      record->end_row, record->end_col);
    restoreCursor(record);
    editorPushRedoBlock(inverse, record->row, record->col,
                        record->end_row, record->end_col,
                        cy, cx, text, length);
}

static void undoDeleteBlock(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy;
    E.cy = record->row;
    E.cx = record->col;
    editorInsertBlock(record->block_text, record->block_len);
    restoreCursor(record);
    editorPushRedoBlock(inverse, record->row, record->col,
                        record->end_row, record->end_col,
                        cy, cx, record->block_text, record->block_len);
    record->block_text = NULL;
}

static void undoAtomicInsert(struct undoRecord *record, int inverse) {
    (void)inverse;
    editorDeleteRange(record->row, record->col,
                      record->end_row, record->end_col);
    restoreCursor(record);
    editorMoveRecordToRedo(record);
}

static void undoAtomicDelete(struct undoRecord *record, int inverse) {
    (void)inverse;
    E.cy = record->row;
    E.cx = record->col;
    editorInsertBlock(record->block_text, record->block_len);
    restoreCursor(record);
    editorMoveRecordToRedo(record);
}

static void undoAtomicReplace(struct undoRecord *record, int inverse) {
    (void)inverse;
    editorDeleteRange(record->row, record->col,
                      record->new_end_row, record->new_end_col);
    E.cy = record->row;
    E.cx = record->col;
    editorInsertBlock(record->block_text, record->block_len);
    restoreCursor(record);
    editorMoveRecordToRedo(record);
}

static void undoIndent(struct undoRecord *record, int inverse) {
    int cx = E.cx, cy = E.cy, length;
    char *text = editorGetTextInRange(
        record->row, 0, record->end_row, E.row[record->end_row].size, &length);
    editorDeleteRange(record->row, 0, record->end_row,
                      E.row[record->end_row].size);
    E.cy = record->row;
    E.cx = 0;
    editorInsertBlock(record->block_text, record->block_len);
    editorPushRedoBlock(inverse, record->row, 0,
                        record->end_row, E.row[record->end_row].size,
                        cy, cx, text, length);
    restoreCursor(record);
}

static void undoReplaceBlock(struct undoRecord *record, int inverse) {
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

    int end_row = record->row, end_col = record->col;
    for (int i = 0; i < record->block_len; i++) {
        if (record->block_text[i] == '\r' && i + 1 < record->block_len &&
            record->block_text[i + 1] == '\n') {
            end_row++;
            end_col = 0;
            i++;
        } else end_col++;
    }
    editorPushRedoBlock(UO_REPLACE_BLOCK, record->row, record->col,
                        end_row, end_col, cy, cx, text, length);
    restoreCursor(record);
}

static void undoMoveBlock(struct undoRecord *record, int inverse) {
    (void)inverse;
    if (!record->block_text || record->block_len <= 0) return;
    int end_row, end_col;
    editorBlockEndPosition(record->dest_row, record->dest_col,
                           record->block_text, record->block_len,
                           &end_row, &end_col);
    editorDeleteRange(record->dest_row, record->dest_col, end_row, end_col);
    E.cy = record->row;
    E.cx = record->col;
    editorInsertBlock(record->block_text, record->block_len);
    editorMoveRecordToRedo(record);
    editorClearSelection();
    restoreCursor(record);
}

void editorReplayUndo(struct undoRecord *record, int inverse) {
    static undoHandler handlers[UO_ATOMIC_MOVE + 1] = {
        [UO_INSERT_CHAR] = undoInsertChar,
        [UO_DELETE_CHAR] = undoDeleteChar,
        [UO_SPLIT_ROW] = undoSplitRow,
        [UO_JOIN_ROW] = undoJoinRow,
        [UO_INSERT_ROW] = undoInsertRow,
        [UO_DELETE_ROW] = undoDeleteRow,
        [UO_INSERT_BLOCK] = undoInsertBlock,
        [UO_DELETE_BLOCK] = undoDeleteBlock,
        [UO_INDENT_LINES] = undoIndent,
        [UO_UNINDENT_LINES] = undoIndent,
        [UO_REPLACE_BLOCK] = undoReplaceBlock,
        [UO_MOVE_BLOCK] = undoMoveBlock,
        [UO_ATOMIC_INSERT] = undoAtomicInsert,
        [UO_ATOMIC_DELETE] = undoAtomicDelete,
        [UO_ATOMIC_REPLACE] = undoAtomicReplace,
        [UO_ATOMIC_MOVE] = undoMoveBlock
    };
    if (record->op_type >= UO_INSERT_CHAR &&
        record->op_type <= UO_ATOMIC_MOVE && handlers[record->op_type])
        handlers[record->op_type](record, inverse);
}
