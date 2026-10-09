#include "common.h"
#include "win_text.h"

wchar_t *editorUtf8ToWide(const char *text) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!count) return NULL;
    wchar_t *wide = malloc(count * sizeof(*wide));
    if (!wide) die("malloc: Unicode text");
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count)) {
        free(wide);
        return NULL;
    }
    return wide;
}

char *editorWideToUtf8(const wchar_t *text) {
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                    NULL, 0, NULL, NULL);
    if (!count) return NULL;
    char *utf8 = malloc(count);
    if (!utf8) die("malloc: UTF-8 text");
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                             utf8, count, NULL, NULL)) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

FILE *editorFopenUtf8(const char *path, const char *mode) {
    wchar_t *wide_path = editorUtf8ToWide(path);
    wchar_t *wide_mode = editorUtf8ToWide(mode);
    FILE *file = wide_path && wide_mode ? _wfopen(wide_path, wide_mode) : NULL;
    free(wide_path);
    free(wide_mode);
    return file;
}

DWORD editorFileAttributesUtf8(const char *path) {
    wchar_t *wide = editorUtf8ToWide(path);
    DWORD attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    free(wide);
    return attributes;
}

char *editorReadClipboardText(int *length) {
    char *text = NULL;
    *length = 0;
    if (!OpenClipboard(NULL)) return NULL;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const wchar_t *source = GlobalLock(data);
        if (source) {
            text = editorWideToUtf8(source);
            if (text) *length = (int)strlen(text);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

int editorWriteClipboardText(const char *text) {
    wchar_t *wide = editorUtf8ToWide(text);
    if (!wide) return 0;
    size_t size = (wcslen(wide) + 1) * sizeof(*wide);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    int copied = 0;
    if (memory) {
        void *target = GlobalLock(memory);
        if (target) {
            memcpy(target, wide, size);
            GlobalUnlock(memory);
            if (OpenClipboard(GetConsoleWindow())) {
                if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory))
                    copied = 1;
                CloseClipboard();
            }
        }
        if (!copied) GlobalFree(memory);
    }
    free(wide);
    return copied;
}
