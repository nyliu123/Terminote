#include "common.h"
#include "output.h"
#include "undo.h"
#include "redo_internal.h"
#include "localization.h"

void editorRedo(void) {
    if (redo_count == 0) {
        editorSetStatusMessage("%s", L("redo.empty"));
        return;
    }

    editorEndUndoGroup();
    undo_suspended++;
    int group_id = redo_stack[redo_count - 1].group_id;
    do {
        struct undoRecord *record = &redo_stack[--redo_count];
        editorReplayRedo(record, editorInverseOp(record->op_type));
        if (group_id && undo_count > 0)
            undo_stack[undo_count - 1].group_id = group_id;
    } while (group_id && redo_count > 0 &&
             redo_stack[redo_count - 1].group_id == group_id);
    editorSetEvent("%s", L("redo.event"));
    if (undo_suspended > 0) undo_suspended--;
}
