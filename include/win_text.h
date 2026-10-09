#ifndef WIN_TEXT_H
#define WIN_TEXT_H
#include <windows.h>
#include <stdio.h>
wchar_t *editorUtf8ToWide(const char *text);
char *editorWideToUtf8(const wchar_t *text);
FILE *editorFopenUtf8(const char *path, const char *mode);
DWORD editorFileAttributesUtf8(const char *path);
char *editorReadClipboardText(int *length);
int editorWriteClipboardText(const char *text);
#endif
