#ifndef TERMINAL_H
#define TERMINAL_H

void enableRawMode(void);
void disableRawMode(void);
int editorPollGlobalShortcut(void);
int  editorReadKey(void);
int  editorReadKeyNonBlocking(void);
char *editorTakePendingPaste(int *len);
int  getWindowSize(int *rows, int *cols);
void editorFitBufferToWindow(void);
void hideConsoleScrollbars(void);

#endif
