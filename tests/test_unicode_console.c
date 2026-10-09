#include "common.h"
#include "win_text.h"
#include <wchar.h>

static PROCESS_INFORMATION child;
static HANDLE input;
static FILE *report;
static const wchar_t *sample_path = L"build/tests/中文 文件名.txt";
static void stopChild(void) {
    if (child.hProcess) {
        if (WaitForSingleObject(child.hProcess, 1000) != WAIT_OBJECT_0) {
            TerminateProcess(child.hProcess, 1);
            WaitForSingleObject(child.hProcess, 3000);
        }
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        child.hProcess = NULL;
    }
}
void die(const char *message) {
    fprintf(report, "FAIL: %s (error %lu)\n", message, GetLastError());
    stopChild();
    DeleteFileW(sample_path);
    fclose(report);
    exit(1);
}
static void key(WORD vk, wchar_t ch, DWORD state) {
    INPUT_RECORD records[2] = {0};
    for (int i = 0; i < 2; i++) {
        records[i].EventType = KEY_EVENT;
        records[i].Event.KeyEvent.bKeyDown = i == 0;
        records[i].Event.KeyEvent.wRepeatCount = 1;
        records[i].Event.KeyEvent.wVirtualKeyCode = vk;
        records[i].Event.KeyEvent.uChar.UnicodeChar = ch;
        records[i].Event.KeyEvent.dwControlKeyState = state;
    }
    DWORD written;
    if (!WriteConsoleInputW(input, records, 2, &written)) die("write input");
    Sleep(120);
}
static void text(const wchar_t *value) {
    for (; *value; value++) key(VK_PACKET, *value, 0);
}
static void mouse(int x, int y, DWORD buttons, DWORD flags) {
    INPUT_RECORD record = {0};
    record.EventType = MOUSE_EVENT;
    record.Event.MouseEvent.dwMousePosition = (COORD){(SHORT)x, (SHORT)y};
    record.Event.MouseEvent.dwButtonState = buttons;
    record.Event.MouseEvent.dwEventFlags = flags;
    DWORD written;
    if (!WriteConsoleInputW(input, &record, 1, &written)) die("write mouse input");
    Sleep(120);
}
static void selectText(int from, int to) {
    mouse(from, 1, FROM_LEFT_1ST_BUTTON_PRESSED, 0);
    mouse(to, 1, FROM_LEFT_1ST_BUTTON_PRESSED, MOUSE_MOVED);
    mouse(to, 1, 0, 0);
}
static char *capture(const char *name) {
    Sleep(250);
    HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(output, &info)) die("console info");
    int width = info.srWindow.Right - info.srWindow.Left + 1;
    int height = info.srWindow.Bottom - info.srWindow.Top + 1;
    COORD size = {(SHORT)width, (SHORT)height}, origin = {0, 0};
    CHAR_INFO *cells = calloc(width * height, sizeof(*cells));
    SMALL_RECT area = info.srWindow;
    if (!ReadConsoleOutputW(output, cells, size, origin, &area)) die("capture console");
    CloseHandle(output);
    wchar_t *screen = calloc(width * height + height + 1, sizeof(*screen));
    int at = 0;
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            CHAR_INFO cell = cells[row * width + col];
            if (!(cell.Attributes & COMMON_LVB_TRAILING_BYTE))
                screen[at++] = cell.Char.UnicodeChar ? cell.Char.UnicodeChar : L' ';
        }
        screen[at++] = L'\n';
    }
    char *utf8 = editorWideToUtf8(screen);
    char path[128];
    snprintf(path, sizeof(path), "build/tests/%s.txt", name);
    FILE *file = fopen(path, "wb");
    fwrite(utf8, 1, strlen(utf8), file);
    fclose(file);
    free(screen);
    free(cells);
    return utf8;
}
static void expect(const char *name, const char *needle) {
    char *screen = capture(name);
    if (!strstr(screen, needle)) die(name);
    free(screen);
    fprintf(report, "%s: PASS\n", name);
    fflush(report);
}

static void expectPromptCursor(const char *name, int input_width) {
    char *screen = capture(name);
    free(screen);
    HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    CONSOLE_SCREEN_BUFFER_INFO info;
    CONSOLE_CURSOR_INFO cursor;
    if (!GetConsoleScreenBufferInfo(output, &info) ||
        !GetConsoleCursorInfo(output, &cursor)) die("prompt cursor info");
    int width = info.srWindow.Right - info.srWindow.Left + 1;
    CHAR_INFO *cells = calloc(width, sizeof(*cells));
    COORD size = {(SHORT)width, 1}, origin = {0, 0};
    SMALL_RECT area = {info.srWindow.Left, info.srWindow.Bottom - 1,
                       info.srWindow.Right, info.srWindow.Bottom - 1};
    if (!ReadConsoleOutputW(output, cells, size, origin, &area)) die("prompt cursor row");
    CloseHandle(output);
    int prefix = 0;
    while (prefix < width && cells[prefix].Char.UnicodeChar != L':') prefix++;
    free(cells);
    if (prefix == width || !cursor.bVisible ||
        info.dwCursorPosition.X != info.srWindow.Left + prefix + 2 + input_width ||
        info.dwCursorPosition.Y != info.srWindow.Bottom - 1) die(name);
    fprintf(report, "%s: PASS (cursor %d,%d visible)\n", name,
            info.dwCursorPosition.X, info.dwCursorPosition.Y);
    fflush(report);
}
static void expectBodyCursor(const char *name, int row, int column) {
    char *screen = capture(name);
    free(screen);
    HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    CONSOLE_SCREEN_BUFFER_INFO info;
    CONSOLE_CURSOR_INFO cursor;
    if (!GetConsoleScreenBufferInfo(output, &info) ||
        !GetConsoleCursorInfo(output, &cursor)) die("body cursor info");
    CloseHandle(output);
    if (!cursor.bVisible || info.dwCursorPosition.X != info.srWindow.Left + column ||
        info.dwCursorPosition.Y != info.srWindow.Top + row) die(name);
    fprintf(report, "%s: PASS (cursor %d,%d visible)\n", name,
            info.dwCursorPosition.X, info.dwCursorPosition.Y);
    fflush(report);
}
int main(void) {
    report = fopen("build/tests/unicode-console-report.txt", "wb");
    if (!report) return 1;
    FILE *sample = editorFopenUtf8("build/tests/中文 文件名.txt", "wb");
    fputs("中文查找 中文查找\r\n复制测试", sample);
    fclose(sample);
    FreeConsole();
    wchar_t command[] = L"terminote.exe \"build/tests/中文 文件名.txt\"";
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &startup, &child)) die("launch editor");
    int attached = 0;
    for (int i = 0; i < 30 && !attached; i++) { Sleep(100); attached = AttachConsole(child.dwProcessId); }
    if (!attached) die("attach console");
    input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    Sleep(1000);
    expect("unicode-filename", "中文 文件名.txt");
    expectBodyCursor("body-file-start-cursor", 1, 4);
    key('N', 14, LEFT_CTRL_PRESSED);
    expectBodyCursor("body-new-empty-cursor", 1, 4);
    text(L"中");
    expectBodyCursor("body-first-chinese-cursor", 1, 6);
    key(VK_BACK, L'\b', 0);
    expectBodyCursor("body-empty-line-cursor", 1, 4);
    key(VK_RETURN, L'\r', 0);
    expectBodyCursor("body-new-line-cursor", 2, 4);
    key('Q', 17, LEFT_CTRL_PRESSED);
    selectText(4, 12);
    key('F', 6, LEFT_CTRL_PRESSED);
    expect("selection-find-prefill", "中文查找 (");
    expectPromptCursor("selection-find-cursor", 8);
    key(VK_BACK, L'\b', 0);
    expectPromptCursor("selection-find-edit-cursor", 6);
    key(VK_ESCAPE, 27, 0);
    mouse(25, 1, FROM_LEFT_1ST_BUTTON_PRESSED, 0);
    mouse(25, 1, 0, 0);
    selectText(12, 4);
    key('R', 18, LEFT_CTRL_PRESSED);
    expect("selection-replace-prefill", "中文查找 (");
    expectPromptCursor("selection-replace-cursor", 8);
    key(VK_RETURN, L'\r', 0);
    expectPromptCursor("selection-replace-with-empty", 0);
    key(VK_ESCAPE, 27, 0);
    key('A', 1, LEFT_CTRL_PRESSED);
    key('F', 6, LEFT_CTRL_PRESSED);
    expect("selection-multiline-first-line", "中文查找 中文查找 (");
    expectPromptCursor("selection-multiline-cursor", 17);
    key(VK_ESCAPE, 27, 0);
    mouse(25, 1, FROM_LEFT_1ST_BUTTON_PRESSED, 0);
    mouse(25, 1, 0, 0);
    key(VK_HOME, 0, 0);
    key('F', 6, LEFT_CTRL_PRESSED);
    expectPromptCursor("find-empty-cursor", 0);
    text(L"中");
    expectPromptCursor("find-chinese-cursor", 2);
    key(VK_BACK, L'\b', 0);
    expectPromptCursor("find-cleared-cursor", 0);
    text(L"中文查找");
    expect("unicode-find-input", "中文查找");
    key(VK_RETURN, L'\r', 0);
    expect("unicode-find-result", "2");
    key(VK_ESCAPE, 27, 0);
    key('R', 18, LEFT_CTRL_PRESSED);
    expectPromptCursor("replace-find-empty-cursor", 0);
    text(L"中文查找");
    key(VK_RETURN, L'\r', 0);
    expectPromptCursor("replace-with-empty-cursor", 0);
    text(L"替换成功");
    expect("unicode-replace-input", "替换成功");
    key(VK_RETURN, L'\r', 0);
    key('A', L'a', 0);
    expect("unicode-replace-result", "替换成功 替换成功");
    key('S', 19, LEFT_CTRL_PRESSED);
    key('Q', 17, LEFT_CTRL_PRESSED);
    if (WaitForSingleObject(child.hProcess, 3000) != WAIT_OBJECT_0) die("editor exit");
    sample = editorFopenUtf8("build/tests/中文 文件名.txt", "rb");
    char contents[128] = {0};
    fread(contents, 1, sizeof(contents) - 1, sample);
    fclose(sample);
    if (strcmp(contents, "替换成功 替换成功\r\n复制测试\r\n")) die("saved contents");
    fprintf(report, "unicode-save: PASS\n");
    stopChild();
    CloseHandle(input);
    FreeConsole();
    DeleteFileW(sample_path);
    fclose(report);
    return 0;
}
