#include "common.h"
#include "restart.h"
#include "paths.h"
#include "tabs.h"
#include "localization.h"
#include "output.h"

#define RESTART_SESSION_ENV "EDITOR_RESTART_SESSION"
#define RESTART_READY_ENV "EDITOR_RESTART_READY_EVENT"

static int cleanup_registered = 0;
static int cleanup_enabled = 0;

static int normalizeSessionManifest(const char *input, char *manifest,
                                    size_t manifest_size,
                                    char *directory, size_t directory_size) {
    char *absolute = editorAbsolutePath(input);
    if (!absolute) return 0;
    char expected[MAX_PATH];
    char temporary[MAX_PATH];
    int valid = editorExecutablePath("temp\\session.txt", expected,
                                     sizeof(expected)) &&
                editorExecutablePath("temp", temporary, sizeof(temporary)) &&
                _stricmp(absolute, expected) == 0;
    size_t absolute_length = strlen(absolute);
    if (!valid || absolute_length >= manifest_size ||
        strlen(temporary) >= directory_size) {
        free(absolute);
        return 0;
    }
    memcpy(manifest, absolute, absolute_length + 1);
    snprintf(directory, directory_size, "%s", temporary);
    free(absolute);
    return 1;
}

static int validateSessionManifest(const char *input, char *manifest,
                                   size_t size) {
    char directory[MAX_PATH];
    return normalizeSessionManifest(input, manifest, size,
                                    directory, sizeof(directory));
}

static void deleteTemporaryContents(const char *directory) {
    char pattern[MAX_PATH];
    if (snprintf(pattern, sizeof(pattern), "%s\\*", directory) >=
        (int)sizeof(pattern))
        return;
    WIN32_FIND_DATAA data;
    HANDLE search = FindFirstFileA(pattern, &data);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(data.cFileName, ".") == 0 ||
                strcmp(data.cFileName, "..") == 0)
                continue;
            char path[MAX_PATH];
            int length = snprintf(path, sizeof(path), "%s\\%s", directory,
                                  data.cFileName);
            if (length < 0 || length >= (int)sizeof(path)) continue;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    deleteTemporaryContents(path);
                    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
                    RemoveDirectoryA(path);
                } else RemoveDirectoryA(path);
            } else {
                SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
                DeleteFileA(path);
            }
        } while (FindNextFileA(search, &data));
        FindClose(search);
    }
}

static void cleanupRestartTemporaryFiles(void) {
    if (!cleanup_enabled) return;
    char temporary[MAX_PATH];
    if (!editorExecutablePath("temp", temporary, sizeof(temporary))) return;
    deleteTemporaryContents(temporary);
    RemoveDirectoryA(temporary);
}

static BOOL WINAPI cleanupOnConsoleClose(DWORD event) {
    if (event != CTRL_CLOSE_EVENT) return FALSE;
    cleanupRestartTemporaryFiles();
    return FALSE;
}

static int isProjectIniPath(const char *path) {
    char directory[MAX_PATH] = "";
    if (!path || !editorExecutableDirectory(directory, sizeof(directory)))
        return 0;
    size_t directory_length = strlen(directory);
    size_t path_length = strlen(path);
    if (path_length < directory_length + 4 ||
        _strnicmp(path, directory, directory_length) != 0)
        return 0;
    return _stricmp(path + path_length - 4, ".ini") == 0;
}

static int createSessionDirectory(char *directory, size_t size) {
    if (!editorExecutablePath("temp", directory, size)) return 0;
    if (!CreateDirectoryA(directory, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
        return 0;
    return 1;
}

int editorRestartSessionRequested(char *manifest, size_t size) {
    char requested[MAX_PATH];
    DWORD length = GetEnvironmentVariableA(RESTART_SESSION_ENV, requested,
                                           sizeof(requested));
    SetEnvironmentVariableA(RESTART_SESSION_ENV, NULL);
    if (length == 0) return 0;
    if (length >= sizeof(requested) ||
        !validateSessionManifest(requested, manifest, size))
        return -1;
    return 1;
}

int editorFindPendingRestartSession(char *manifest, size_t size) {
    char requested[MAX_PATH];
    if (!editorExecutablePath("temp\\session.txt", requested,
                              sizeof(requested)))
        return 0;
    DWORD attributes = GetFileAttributesA(requested);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    return validateSessionManifest(requested, manifest, size);
}

void editorMarkRestartSessionRestored(void) {
    cleanup_enabled = 1;
    if (cleanup_registered) return;
    cleanup_registered = 1;
    atexit(cleanupRestartTemporaryFiles);
    SetConsoleCtrlHandler(cleanupOnConsoleClose, TRUE);
}

void editorSignalRestartReady(void) {
    char event_name[128];
    DWORD length = GetEnvironmentVariableA(RESTART_READY_ENV, event_name,
                                           sizeof(event_name));
    SetEnvironmentVariableA(RESTART_READY_ENV, NULL);
    if (length == 0 || length >= sizeof(event_name)) return;

    HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, event_name);
    if (!event) return;
    SetEvent(event);
    CloseHandle(event);
}

void editorRestartAfterProjectConfigSave(void) {
    if (!isProjectIniPath(E.filename)) return;

    char directory[MAX_PATH] = "";
    char manifest[MAX_PATH];
    if (!createSessionDirectory(directory, sizeof(directory)) ||
        snprintf(manifest, sizeof(manifest), "%s\\session.txt", directory) >=
            (int)sizeof(manifest) ||
        !editorTabsWriteRestartSession(directory, manifest)) {
        editorSetStatusMessage(L("restart.snapshot_failed"), directory);
        return;
    }
    /* From this point temp/ is the only restart-recovery snapshot.  Preserve
       it until a later process restores the session successfully. */
    cleanup_enabled = 0;

    char event_name[128];
    snprintf(event_name, sizeof(event_name),
             "Local\\EditorRestart-%lu-%llu",
             (unsigned long)GetCurrentProcessId(),
             (unsigned long long)GetTickCount64());
    HANDLE ready = CreateEventA(NULL, TRUE, FALSE, event_name);
    if (!ready) {
        editorSetStatusMessage(L("restart.failed"), directory);
        return;
    }

    char executable[MAX_PATH];
    DWORD executable_length = GetModuleFileNameA(NULL, executable,
                                                 sizeof(executable));
    char command[MAX_PATH + 3];
    if (executable_length == 0 || executable_length >= sizeof(executable) ||
        snprintf(command, sizeof(command), "\"%s\"", executable) >=
            (int)sizeof(command)) {
        CloseHandle(ready);
        editorSetStatusMessage(L("restart.failed"), directory);
        return;
    }

    if (!SetEnvironmentVariableA(RESTART_SESSION_ENV, manifest) ||
        !SetEnvironmentVariableA(RESTART_READY_ENV, event_name)) {
        SetEnvironmentVariableA(RESTART_SESSION_ENV, NULL);
        SetEnvironmentVariableA(RESTART_READY_ENV, NULL);
        CloseHandle(ready);
        editorSetStatusMessage(L("restart.failed"), directory);
        return;
    }
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    BOOL started = CreateProcessA(executable, command, NULL, NULL, FALSE,
                                  CREATE_NEW_CONSOLE, NULL, NULL,
                                  &startup, &process);
    SetEnvironmentVariableA(RESTART_SESSION_ENV, NULL);
    SetEnvironmentVariableA(RESTART_READY_ENV, NULL);
    if (!started) {
        CloseHandle(ready);
        editorSetStatusMessage(L("restart.failed"), directory);
        return;
    }

    HANDLE waits[2] = {ready, process.hProcess};
    DWORD result = WaitForMultipleObjects(2, waits, FALSE, 10000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(ready);
    if (result == WAIT_OBJECT_0) exit(0);

    editorSetStatusMessage(L("restart.failed"), directory);
}
