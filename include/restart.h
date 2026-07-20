#ifndef RESTART_H
#define RESTART_H

int editorRestartSessionRequested(char *manifest, size_t size);
int editorFindPendingRestartSession(char *manifest, size_t size);
void editorMarkRestartSessionRestored(void);
void editorSignalRestartReady(void);
void editorRestartAfterProjectConfigSave(void);

#endif
