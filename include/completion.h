#ifndef COMPLETION_H
#define COMPLETION_H

void editorResetCompletion(void);
void editorUpdateCompletion(void);
int editorCompletionActive(void);
int editorHandleCompletionInput(int key);
void editorDrawCompletionPopup(void);

#endif
