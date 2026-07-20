#ifndef UNDO_H
#define UNDO_H

void editorPushUndo(int op_type, int row, int col,
                    int c_row, int c_col,
                    const char *save, int save_len);
void editorBeginTypingUndoGroup(void);
void editorEndUndoGroup(void);
void editorPushRedo(int op_type, int row, int col,
                    int c_row, int c_col,
                    const char *save, int save_len);
void editorPushUndoBlock(int op, int sr, int sc, int er, int ec,
                         int cur_row, int cur_col,
                         char *text, int text_len);
void editorSetLastUndoResultCursor(int row, int col);
void editorPushUndoMoveBlock(int sr, int sc, int er, int ec,
                             int dest_row, int dest_col,
                             int cur_row, int cur_col,
                             char *text, int text_len);
void editorPushUndoAtomicReplace(int sr, int sc,
                                 int old_er, int old_ec,
                                 int new_er, int new_ec,
                                 int cur_row, int cur_col,
                                 char *old_text, int old_len,
                                 char *new_text, int new_len);
void editorUndo(void);
void editorRedo(void);
int  editorInverseOp(int op);

#endif
