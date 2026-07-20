#ifndef OUTPUT_H
#define OUTPUT_H

#include "common.h"

void abAppend(const char *s);
void abWrite(void);
void abFree(void);
void editorDrawRows(void);
void editorDrawStatusBar(void);
void editorDrawEventBar(void);
void editorRefreshScreen(void);
void editorSetStatusMessage(const char *fmt, ...);
void editorSetEvent(const char *fmt, ...);
void editorHelp(void);
void editorHelpMinimumSize(int *rows, int *cols);
int  editorVisibleRows(void);
int  editorMaxRenderWidth(int minimum);

#endif
