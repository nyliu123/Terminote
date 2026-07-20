#ifndef EDITOR_H
#define EDITOR_H

void editorInsertChar(int c);
void editorInsertNewline(void);
void editorInsertNewlineWithIndent(void);
void editorInsertConfiguredTab(void);
int  editorInsertAutoPair(int c);
int  editorDeleteAutoPair(void);
void editorDelChar(void);
void editorMoveCursor(int key);
void editorScroll(void);
void editorProcessKeypress(void);

/* ---- selection ---- */
void  editorClearSelection(void);
void  editorStartSelection(void);
char *editorGetSelectedText(int *outlen);
void  editorDeleteSelection(void);
void  editorCutSelection(void);
int   editorPosInSelection(int row, int col);
char *editorGetTextInRange(int sr, int sc, int er, int ec, int *outlen);
void  editorDeleteRange(int sr, int sc, int er, int ec);
void  editorInsertBlock(char *text, int len);
void  editorIndentSelection(void);
void  editorUnindentSelection(void);

#endif
