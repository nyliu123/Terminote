#include "common.h"
#include "win_text.h"
#include <assert.h>
#include <wchar.h>

void die(const char *message) { fprintf(stderr, "%s\n", message); exit(1); }

int main(void) {
    const char *sample = "中文 文件名 é 😀\r\n查找与替换";
    wchar_t *wide = editorUtf8ToWide(sample);
    assert(wide);
    char *roundtrip = editorWideToUtf8(wide);
    assert(roundtrip && strcmp(sample, roundtrip) == 0);
    free(roundtrip);
    free(wide);
    assert(!editorUtf8ToWide("\xff"));
    wchar_t invalid[] = {0xd800, 0};
    assert(!editorWideToUtf8(invalid));
    const char *path = "build/tests/中文 文件名.txt";
    FILE *file = editorFopenUtf8(path, "wb");
    assert(file);
    assert(fwrite(sample, 1, strlen(sample), file) == strlen(sample));
    assert(fclose(file) == 0);
    assert(editorFileAttributesUtf8(path) != INVALID_FILE_ATTRIBUTES);
    file = editorFopenUtf8(path, "rb");
    assert(file);
    char readback[128] = {0};
    assert(fread(readback, 1, sizeof(readback) - 1, file) == strlen(sample));
    fclose(file);
    assert(strcmp(readback, sample) == 0);
    wide = editorUtf8ToWide(path);
    assert(DeleteFileW(wide));
    free(wide);
    puts("test_win_text: PASS");
    return 0;
}
