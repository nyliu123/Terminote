#include "common.h"
#include "execution.h"
#include "file.h"
#include "output.h"
#include "paths.h"
#include "logger.h"
#include "localization.h"
#include "ini_config.h"

#define MAX_EXECUTION_COMMANDS 64

struct executionCommand {
    char extension[32];
    char *command;
};

static struct executionCommand commands[MAX_EXECUTION_COMMANDS];
static int command_count = 0;

static _Noreturn void executionError(int line, const char *message,
                                     const char *detail) {
    if (detail && detail[0])
        loggerFatal("config\\execution.ini:%d: %s: %s", line, message, detail);
    loggerFatal("config\\execution.ini:%d: %s", line, message);
}

static int validPlaceholder(const char *name, int length) {
    static const char *names[] = {
        "file_dir", "file_path", "file_stem", "file_name",
        "file_name_no_ext", "file_ext", NULL
    };
    for (int i = 0; names[i]; i++)
        if ((int)strlen(names[i]) == length &&
            strncmp(name, names[i], length) == 0) return 1;
    return 0;
}

static void validateTemplate(int line, const char *text) {
    for (const char *at = text; *at; at++) {
        if (*at == '}') executionError(line, L("execution.unmatched_close"), text);
        if (*at != '{') continue;
        const char *end = strchr(at + 1, '}');
        if (!end) executionError(line, L("execution.placeholder_close"), text);
        if (!validPlaceholder(at + 1, (int)(end - at - 1)))
            executionError(line, L("execution.unknown_placeholder"), at);
        at = end;
    }
}

void editorLoadExecutionCommands(void) {
    char path[MAX_PATH];
    if (!editorExecutablePath(EDITOR_EXECUTION_FILE, path, sizeof(path)))
        loggerFatal("%s", L("execution.config_path_long"));
    command_count = 0;
    int in_commands = 0;
    struct iniDocument document;
    struct iniError error;
    if (!iniDocumentLoad(path, 4096, &document, &error)) {
        if (error.kind == INI_ERROR_OPEN)
            loggerFatal("%s", L("execution.open_failed"));
        if (error.kind == INI_ERROR_LINE_TOO_LONG)
            executionError(error.line, L("execution.line_long"), NULL);
        if (error.kind == INI_ERROR_MISSING_EQUALS)
            executionError(error.line, L("config.missing_equals"), error.text);
        executionError(error.line, L("config.unknown_section"), error.text);
    }
    for (int item_index = 0; item_index < document.count; item_index++) {
        struct iniItem *item = &document.items[item_index];
        int line = item->line;
        if (item->type == INI_ITEM_SECTION) {
            in_commands = _stricmp(item->section, "commands") == 0;
            if (!in_commands)
                executionError(line, L("config.unknown_section"), item->section);
            continue;
        }
        if (!in_commands)
            executionError(line, L("execution.outside_section"), item->text);
        char *extension = item->key;
        char *command = item->value;
        if (extension[0] == '.') extension++;
        if (!extension[0] || strlen(extension) >= sizeof(commands[0].extension))
            executionError(line, L("execution.extension_length"), extension);
        for (const char *at = extension; *at; at++)
            if (!isalnum((unsigned char)*at) && *at != '_' && *at != '-')
                executionError(line, L("execution.invalid_extension"), extension);
        if (!command[0]) executionError(line, L("execution.empty_command"), extension);
        validateTemplate(line, command);
        for (int i = 0; i < command_count; i++)
            if (_stricmp(commands[i].extension, extension) == 0)
                executionError(line, L("execution.duplicate_extension"), extension);
        if (command_count >= MAX_EXECUTION_COMMANDS)
            executionError(line, L("execution.too_many"), extension);
        snprintf(commands[command_count].extension,
                 sizeof(commands[command_count].extension), "%s", extension);
        commands[command_count].command = editor_strdup(command);
        command_count++;
    }
    iniDocumentFree(&document);
    loggerLog(LOG_INFO, L("log.execution_validated"), command_count);
}

static void appendValue(char **buffer, size_t *length, size_t *capacity,
                        const char *value) {
    size_t add = strlen(value);
    if (*length + add + 1 > *capacity) {
        while (*length + add + 1 > *capacity) *capacity *= 2;
        *buffer = realloc(*buffer, *capacity);
        if (!*buffer) die("realloc: execution command");
    }
    memcpy(*buffer + *length, value, add);
    *length += add;
    (*buffer)[*length] = '\0';
}

static const char *placeholderValue(const char *name, int length,
                                    const char *dir, const char *path,
                                    const char *stem, const char *filename,
                                    const char *name_no_ext, const char *ext) {
    if (length == 8 && strncmp(name, "file_dir", 8) == 0) return dir;
    if (length == 9 && strncmp(name, "file_path", 9) == 0) return path;
    if (length == 9 && strncmp(name, "file_stem", 9) == 0) return stem;
    if (length == 9 && strncmp(name, "file_name", 9) == 0) return filename;
    if (length == 16 && strncmp(name, "file_name_no_ext", 16) == 0)
        return name_no_ext;
    if (length == 8 && strncmp(name, "file_ext", 8) == 0) return ext;
    return "";
}

static char *expandCommand(const char *source, const char *dir,
                           const char *path, const char *stem,
                           const char *filename, const char *name_no_ext,
                           const char *extension) {
    size_t capacity = strlen(source) + strlen(path) + 128, length = 0;
    char *result = malloc(capacity);
    if (!result) die("malloc: execution command");
    result[0] = '\0';
    for (const char *at = source; *at;) {
        if (*at != '{') {
            char character[2] = {*at++, '\0'};
            appendValue(&result, &length, &capacity, character);
            continue;
        }
        const char *end = strchr(at + 1, '}');
        const char *value = placeholderValue(at + 1, (int)(end - at - 1),
                                             dir, path, stem, filename,
                                             name_no_ext, extension);
        appendValue(&result, &length, &capacity, value);
        at = end + 1;
    }
    return result;
}

void editorRunCurrentFile(void) {
    if (!E.filename) {
        editorSetStatusMessage("%s", L("run.save_first"));
        return;
    }
    if ((E.dirty || GetFileAttributesA(E.filename) == INVALID_FILE_ATTRIBUTES) &&
        !editorSave()) return;

    const char *filename = strrchr(E.filename, '\\');
    const char *forward = strrchr(E.filename, '/');
    if (forward && (!filename || forward > filename)) filename = forward;
    filename = filename ? filename + 1 : E.filename;
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename || !dot[1]) {
        editorSetStatusMessage("%s", L("run.no_extension"));
        return;
    }
    const struct executionCommand *selected = NULL;
    for (int i = 0; i < command_count; i++)
        if (_stricmp(commands[i].extension, dot + 1) == 0) {
            selected = &commands[i]; break;
        }
    if (!selected) {
        editorSetStatusMessage(L("run.not_configured"), dot + 1);
        return;
    }

    char *dir = editor_strdup(E.filename);
    char *last_slash = strrchr(dir, '\\');
    char *last_forward = strrchr(dir, '/');
    if (last_forward && (!last_slash || last_forward > last_slash))
        last_slash = last_forward;
    if (last_slash) {
        if (last_slash == dir + 2 && dir[1] == ':') last_slash[1] = '\0';
        else *last_slash = '\0';
    } else snprintf(dir, strlen(dir) + 1, ".");

    char *stem = editor_strdup(E.filename);
    stem[dot - filename + (filename - E.filename)] = '\0';
    char *name_no_ext = editor_strdup(filename);
    name_no_ext[dot - filename] = '\0';
    char *expanded = expandCommand(selected->command, dir, E.filename, stem,
                                   filename, name_no_ext, dot + 1);
    size_t line_length = strlen(expanded) + 32;
    char *command_line = malloc(line_length);
    if (!command_line) die("malloc: command line");
    snprintf(command_line, line_length, "cmd.exe /d /s /c \"%s\"", expanded);

    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    BOOL started = CreateProcessA(NULL, command_line, NULL, NULL, FALSE,
                                  CREATE_NEW_CONSOLE, NULL, dir,
                                  &startup, &process);
    if (started) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        editorSetEvent(L("run.event"), dot + 1);
        editorSetStatusMessage(L("run.started"), expanded);
    } else {
        editorSetStatusMessage(L("run.failed"), GetLastError());
    }
    free(command_line); free(expanded); free(name_no_ext); free(stem); free(dir);
}
