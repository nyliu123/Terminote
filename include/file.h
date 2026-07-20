#ifndef FILE_H
#define FILE_H

void editorOpen(const char *filename);
int editorSave(void);
int editorSaveAs(void);
char *editorChooseOpenPath(void);

#endif
