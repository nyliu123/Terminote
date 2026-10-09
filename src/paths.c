#include "common.h"
#include "win_text.h"
#include "paths.h"

int editorExecutableDirectory(char *directory, size_t size) {
    DWORD length = GetModuleFileNameA(NULL, directory, (DWORD)size);
    if (length == 0 || length >= size) return 0;

    char *slash = strrchr(directory, '\\');
    char *forward_slash = strrchr(directory, '/');
    if (!slash || (forward_slash && forward_slash > slash))
        slash = forward_slash;
    if (slash) slash[1] = '\0';
    else directory[0] = '\0';
    return 1;
}

int editorExecutablePath(const char *relative, char *path, size_t size) {
    char directory[MAX_PATH];
    if (!editorExecutableDirectory(directory, sizeof(directory))) return 0;
    return snprintf(path, size, "%s%s", directory, relative) < (int)size;
}

char *editorAbsolutePath(const char *path) {
    if (!path || !path[0]) return NULL;

    wchar_t *wide = editorUtf8ToWide(path);
    if (!wide) return NULL;
    DWORD required = GetFullPathNameW(wide, 0, NULL, NULL);
    if (!required) { free(wide); return NULL; }
    wchar_t *absolute = malloc(required * sizeof(*absolute));
    if (!absolute) die("malloc: absolute path");
    DWORD length = GetFullPathNameW(wide, required, absolute, NULL);
    free(wide);
    char *result = length && length < required ? editorWideToUtf8(absolute) : NULL;
    free(absolute);
    return result;
}
