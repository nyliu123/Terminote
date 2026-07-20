#ifndef ROW_H
#define ROW_H

void editorUpdateRow(erow *row);
void editorUpdateSyntax(erow *row);
void editorInsertRow(int at, char *s, size_t len);
void editorDelRow(int at);
void editorFreeRow(erow *row);
void editorRowInsertChar(erow *row, int at, int c);
void editorRowDelChar(erow *row, int at);
void editorRowAppendString(erow *row, char *s, size_t len);
char *editorRowsToString(int *buflen);
int  editorRowCxToRx(erow *row, int cx);
int  editorRowCxToRenderIndex(erow *row, int cx);
int  editorRowRxToCx(erow *row, int rx);
void editorUpdateLineNumWidth(void);

#endif
