#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include "common.h"
#include "undo.h"
#include "undo_internal.h"
#include "redo_internal.h"

struct editorConfig E;
struct undoRecord *undo_stack = NULL;
int undo_count = 0;
int undo_cap = 0;
struct undoRecord *redo_stack = NULL;
int redo_count = 0;
int redo_cap = 0;
int undo_suspended = 0;

static int undo_replays = 0;
static int redo_replays = 0;

void die(const char *message) {
    fputs(message, stderr);
    abort();
}

const char *localizationGet(const char *key) {
    return key;
}

void editorSetStatusMessage(const char *format, ...) {
    (void)format;
}

void editorSetEvent(const char *format, ...) {
    (void)format;
}

void editorReplayUndo(struct undoRecord *record, int inverse) {
    undo_replays++;
    editorPushRedo(inverse, record->row, record->col,
                   record->cursor_row, record->cursor_col, NULL, 0);
}

void editorReplayRedo(struct undoRecord *record, int inverse) {
    redo_replays++;
    editorPushUndo(inverse, record->row, record->col,
                   record->cursor_row, record->cursor_col, NULL, 0);
}

static void pushTyping(const char *text) {
    editorBeginTypingUndoGroup();
    for (int i = 0; text[i]; i++)
        editorPushUndo(UO_INSERT_CHAR, 0, i, 0, i, NULL, 0);
    editorEndUndoGroup();
}

int main(void) {
    pushTyping("xxx+yyy");
    assert(undo_count == 7);
    int first_group = undo_stack[0].group_id;
    assert(first_group != 0);
    for (int i = 1; i < undo_count; i++)
        assert(undo_stack[i].group_id == first_group);

    editorUndo();
    assert(undo_count == 0);
    assert(redo_count == 7);
    assert(undo_replays == 7);

    editorRedo();
    assert(undo_count == 7);
    assert(redo_count == 0);
    assert(redo_replays == 7);

    pushTyping("z");
    assert(undo_count == 8);
    assert(undo_stack[7].group_id != first_group);
    editorUndo();
    assert(undo_count == 7);

    free(undo_stack);
    free(redo_stack);
    puts("test_undo_group: PASS");
    return 0;
}
