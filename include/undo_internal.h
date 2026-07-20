#ifndef UNDO_INTERNAL_H
#define UNDO_INTERNAL_H

void editorMoveRecordToUndo(struct undoRecord *record);
void editorMoveRecordToRedo(struct undoRecord *record);
void editorPushRedoBlock(int op, int sr, int sc, int er, int ec,
                         int cur_row, int cur_col,
                         char *text, int text_len);
void editorBlockEndPosition(int row, int col, const char *text, int length,
                            int *end_row, int *end_col);
void editorReplayUndo(struct undoRecord *record, int inverse);

#endif
