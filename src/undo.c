#include "common.h"
#include "row.h"
#include "output.h"
#include "editor.h"
#include "undo.h"
#include "undo_internal.h"
#include "localization.h"

static void editorFreeRecordText(struct undoRecord *rec) {
    free(rec->block_text);
    free(rec->new_text);
    rec->block_text = NULL;
    rec->new_text = NULL;
}

static void editorClearRedo(void) {
    for (int i = 0; i < redo_count; i++)
        editorFreeRecordText(&redo_stack[i]);
    redo_count = 0;
}

static int next_undo_group_id = 1;
static int active_typing_group_id = 0;

void editorBeginTypingUndoGroup(void) {
    if (active_typing_group_id) return;
    active_typing_group_id = next_undo_group_id;
    next_undo_group_id = next_undo_group_id == INT_MAX
                         ? 1 : next_undo_group_id + 1;
}

void editorEndUndoGroup(void) {
    active_typing_group_id = 0;
}

void editorMoveRecordToRedo(struct undoRecord *rec) {
    if (redo_count >= redo_cap) {
        redo_cap = redo_cap ? redo_cap * 2 : 256;
        redo_stack = realloc(redo_stack, redo_cap * sizeof(struct undoRecord));
        if (!redo_stack) die("realloc: redo_stack");
    }
    redo_stack[redo_count++] = *rec;
    rec->block_text = NULL;
    rec->new_text = NULL;
}

void editorMoveRecordToUndo(struct undoRecord *rec) {
    if (undo_count >= undo_cap) {
        undo_cap = undo_cap ? undo_cap * 2 : 256;
        undo_stack = realloc(undo_stack, undo_cap * sizeof(struct undoRecord));
        if (!undo_stack) die("realloc: undo_stack");
    }
    undo_stack[undo_count++] = *rec;
    rec->block_text = NULL;
    rec->new_text = NULL;
}

/* ---- push records ---- */
void editorPushUndo(int op_type, int row, int col,
                    int c_row, int c_col,
                    const char *save, int save_len) {
    /* caller is responsible for checking undo_suspended.
       we still clear the redo stack --but only when NOT inside
       an undo/redo operation (undo_suspended == 0).           */
    if (undo_count >= undo_cap) {
        undo_cap = undo_cap ? undo_cap * 2 : 256;
        undo_stack = realloc(undo_stack, undo_cap * sizeof(struct undoRecord));
        if (!undo_stack) die("realloc: undo_stack");
    }
    struct undoRecord *rec = &undo_stack[undo_count++];
    rec->op_type     = op_type;
    rec->group_id    = !undo_suspended && op_type == UO_INSERT_CHAR
                       ? active_typing_group_id : 0;
    rec->row         = row;
    rec->col         = col;
    rec->cursor_row  = c_row;
    rec->cursor_col  = c_col;
    rec->block_text  = NULL;
    rec->block_len   = 0;
    rec->dest_row    = 0;
    rec->dest_col    = 0;
    rec->new_text    = NULL;
    rec->new_len     = 0;
    rec->new_end_row = 0;
    rec->new_end_col = 0;
    rec->result_row  = -1;
    rec->result_col  = -1;
    if (save && save_len > 0) {
        memcpy(rec->save, save, save_len < 4 ? save_len : 3);
        rec->save_len = save_len < 4 ? save_len : 3;
    } else {
        rec->save_len = 0;
    }
    if (!undo_suspended)
        editorClearRedo();
}

void editorPushRedo(int op_type, int row, int col,
                    int c_row, int c_col,
                    const char *save, int save_len) {
    if (redo_count >= redo_cap) {
        redo_cap = redo_cap ? redo_cap * 2 : 256;
        redo_stack = realloc(redo_stack, redo_cap * sizeof(struct undoRecord));
        if (!redo_stack) die("realloc: redo_stack");
    }
    struct undoRecord *rec = &redo_stack[redo_count++];
    rec->op_type     = op_type;
    rec->group_id    = 0;
    rec->row         = row;
    rec->col         = col;
    rec->cursor_row  = c_row;
    rec->cursor_col  = c_col;
    rec->block_text  = NULL;
    rec->block_len   = 0;
    rec->dest_row    = 0;
    rec->dest_col    = 0;
    rec->new_text    = NULL;
    rec->new_len     = 0;
    rec->new_end_row = 0;
    rec->new_end_col = 0;
    rec->result_row  = -1;
    rec->result_col  = -1;
    if (save && save_len > 0) {
        memcpy(rec->save, save, save_len < 4 ? save_len : 3);
        rec->save_len = save_len < 4 ? save_len : 3;
    } else {
        rec->save_len = 0;
    }
}

/* push a block-level undo record (paste, drag-drop, etc.).
   takes ownership of `text` --caller must NOT free it.          */
void editorPushUndoBlock(int op, int sr, int sc, int er, int ec,
                         int cur_row, int cur_col,
                         char *text, int text_len) {
    if (undo_count >= undo_cap) {
        undo_cap = undo_cap ? undo_cap * 2 : 256;
        undo_stack = realloc(undo_stack, undo_cap * sizeof(struct undoRecord));
        if (!undo_stack) die("realloc: undo_stack");
    }
    struct undoRecord *rec = &undo_stack[undo_count++];
    rec->op_type     = op;
    rec->group_id    = 0;
    rec->row         = sr;
    rec->col         = sc;
    rec->cursor_row  = cur_row;
    rec->cursor_col  = cur_col;
    rec->end_row     = er;
    rec->end_col     = ec;
    rec->block_text  = text;
    rec->block_len   = text_len;
    rec->dest_row    = 0;
    rec->dest_col    = 0;
    rec->new_text    = NULL;
    rec->new_len     = 0;
    rec->new_end_row = 0;
    rec->new_end_col = 0;
    rec->result_row  = -1;
    rec->result_col  = -1;
    rec->save_len    = 0;
    if (!undo_suspended)
        editorClearRedo();
}

void editorSetLastUndoResultCursor(int row, int col) {
    if (undo_count <= 0) return;
    undo_stack[undo_count - 1].result_row = row;
    undo_stack[undo_count - 1].result_col = col;
}

void editorPushUndoMoveBlock(int sr, int sc, int er, int ec,
                             int dest_row, int dest_col,
                             int cur_row, int cur_col,
                             char *text, int text_len) {
    if (undo_count >= undo_cap) {
        undo_cap = undo_cap ? undo_cap * 2 : 256;
        undo_stack = realloc(undo_stack, undo_cap * sizeof(struct undoRecord));
        if (!undo_stack) die("realloc: undo_stack");
    }
    struct undoRecord *rec = &undo_stack[undo_count++];
    rec->op_type     = UO_ATOMIC_MOVE;
    rec->group_id    = 0;
    rec->row         = sr;
    rec->col         = sc;
    rec->cursor_row  = cur_row;
    rec->cursor_col  = cur_col;
    rec->end_row     = er;
    rec->end_col     = ec;
    rec->dest_row    = dest_row;
    rec->dest_col    = dest_col;
    rec->block_text  = text;
    rec->block_len   = text_len;
    rec->new_text    = NULL;
    rec->new_len     = 0;
    rec->new_end_row = 0;
    rec->new_end_col = 0;
    rec->result_row  = -1;
    rec->result_col  = -1;
    rec->save_len    = 0;
    if (!undo_suspended)
        editorClearRedo();
}

void editorPushUndoAtomicReplace(int sr, int sc,
                                 int old_er, int old_ec,
                                 int new_er, int new_ec,
                                 int cur_row, int cur_col,
                                 char *old_text, int old_len,
                                 char *new_text, int new_len) {
    if (undo_count >= undo_cap) {
        undo_cap = undo_cap ? undo_cap * 2 : 256;
        undo_stack = realloc(undo_stack, undo_cap * sizeof(struct undoRecord));
        if (!undo_stack) die("realloc: undo_stack");
    }
    struct undoRecord *rec = &undo_stack[undo_count++];
    rec->op_type     = UO_ATOMIC_REPLACE;
    rec->group_id    = 0;
    rec->row         = sr;
    rec->col         = sc;
    rec->cursor_row  = cur_row;
    rec->cursor_col  = cur_col;
    rec->end_row     = old_er;
    rec->end_col     = old_ec;
    rec->dest_row    = 0;
    rec->dest_col    = 0;
    rec->block_text  = old_text;
    rec->block_len   = old_len;
    rec->new_text    = new_text;
    rec->new_len     = new_len;
    rec->new_end_row = new_er;
    rec->new_end_col = new_ec;
    rec->result_row  = -1;
    rec->result_col  = -1;
    rec->save_len    = 0;
    if (!undo_suspended)
        editorClearRedo();
}

void editorPushRedoBlock(int op, int sr, int sc, int er, int ec,
                         int cur_row, int cur_col,
                         char *text, int text_len) {
    if (redo_count >= redo_cap) {
        redo_cap = redo_cap ? redo_cap * 2 : 256;
        redo_stack = realloc(redo_stack, redo_cap * sizeof(struct undoRecord));
        if (!redo_stack) die("realloc: redo_stack");
    }
    struct undoRecord *rec = &redo_stack[redo_count++];
    rec->op_type     = op;
    rec->group_id    = 0;
    rec->row         = sr;
    rec->col         = sc;
    rec->cursor_row  = cur_row;
    rec->cursor_col  = cur_col;
    rec->end_row     = er;
    rec->end_col     = ec;
    rec->block_text  = text;
    rec->block_len   = text_len;
    rec->dest_row    = 0;
    rec->dest_col    = 0;
    rec->new_text    = NULL;
    rec->new_len     = 0;
    rec->new_end_row = 0;
    rec->new_end_col = 0;
    rec->result_row  = -1;
    rec->result_col  = -1;
    rec->save_len    = 0;
}

void editorBlockEndPosition(int row, int col,
                            const char *text, int len,
                            int *end_row, int *end_col) {
    int er = row, ec = col;

    for (int i = 0; i < len; i++) {
        if (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') {
            er++;
            ec = 0;
            i++;
        } else if (text[i] == '\r' || text[i] == '\n') {
            er++;
            ec = 0;
        } else {
            ec++;
        }
    }

    *end_row = er;
    *end_col = ec;
}

/* ---- inverse operation ---- */
int editorInverseOp(int op) {
    _Static_assert(UO_DELETE_CHAR == UO_INSERT_CHAR + 1 &&
                   UO_JOIN_ROW == UO_SPLIT_ROW + 1 &&
                   UO_DELETE_ROW == UO_INSERT_ROW + 1 &&
                   UO_DELETE_BLOCK == UO_INSERT_BLOCK + 1 &&
                   UO_UNINDENT_LINES == UO_INDENT_LINES + 1,
                   "inverse undo operations must remain adjacent");
    return op >= UO_INSERT_CHAR && op <= UO_UNINDENT_LINES
        ? op + (op % 2 ? 1 : -1) : op;
}

/* ---- undo ---- */
void editorUndo(void) {
    if (undo_count == 0) {
        editorSetStatusMessage("%s", L("undo.empty"));
        return;
    }

    editorEndUndoGroup();
    undo_suspended++;
    int group_id = undo_stack[undo_count - 1].group_id;
    do {
        struct undoRecord *record = &undo_stack[--undo_count];
        editorReplayUndo(record, editorInverseOp(record->op_type));
        if (group_id && redo_count > 0)
            redo_stack[redo_count - 1].group_id = group_id;
    } while (group_id && undo_count > 0 &&
             undo_stack[undo_count - 1].group_id == group_id);
    editorSetEvent("%s", L("undo.event"));
    if (undo_suspended > 0) undo_suspended--;
}

