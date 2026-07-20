#include "common.h"
#include "cli.h"
#include "logger.h"
#include "paths.h"
#include "localization.h"
#include "brand.h"

static int optionEquals(const char *argument, const char *long_name,
                        const char *short_name) {
    return strcmp(argument, long_name) == 0 ||
           (short_name && strcmp(argument, short_name) == 0);
}

int cliHasVersionOption(int argc, char **argv) {
    for (int i = 1; i < argc; i++)
        if (optionEquals(argv[i], "--version", "-V")) return 1;
    return 0;
}

int cliHasHelpOption(int argc, char **argv) {
    for (int i = 1; i < argc; i++)
        if (optionEquals(argv[i], "--help", "-h")) return 1;
    return 0;
}

int cliHasCleanOption(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) break;
        if (strcmp(argv[i], "--clean") == 0) return 1;
    }
    return 0;
}

int cliWantsVerbose(int argc, char **argv) {
    for (int i = 1; i < argc; i++)
        if (optionEquals(argv[i], "--verbose", "-v")) return 1;
    return 0;
}

int cliParse(int argc, char **argv, struct cliOptions *options,
             char *error, size_t error_size) {
    int end_options = 0;
    int config_seen = 0;
    memset(options, 0, sizeof(*options));

    for (int i = 1; i < argc; i++) {
        const char *argument = argv[i];
        if (!end_options && strcmp(argument, "--") == 0) {
            end_options = 1;
            continue;
        }
        if (!end_options && optionEquals(argument, "--verbose", "-v")) {
            if (options->verbose) {
                snprintf(error, error_size, L("cli.duplicate_option"), argument);
                return 0;
            }
            options->verbose = 1;
            continue;
        }
        if (!end_options && strncmp(argument, "--config=", 9) == 0) {
            if (config_seen) {
                snprintf(error, error_size, L("cli.duplicate_config"));
                return 0;
            }
            options->config_mode = 1;
            options->config_name = argument + 9;
            config_seen = 1;
            continue;
        }
        if (!end_options && strcmp(argument, "--config") == 0) {
            if (config_seen) {
                snprintf(error, error_size, L("cli.duplicate_config"));
                return 0;
            }
            options->config_mode = 1;
            config_seen = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                options->config_name = argv[++i];
            continue;
        }
        if (!end_options && argument[0] == '-') {
            snprintf(error, error_size, L("cli.unknown_option"), argument);
            return 0;
        }
        if (options->file_count >= 64) {
            snprintf(error, error_size, "%s", L("cli.too_many_files"));
            return 0;
        }
        options->filenames[options->file_count++] = argument;
    }

    if (options->config_mode && options->file_count > 0) {
        snprintf(error, error_size, "%s", L("cli.config_with_files"));
        return 0;
    }
    if (options->config_name && !options->config_name[0]) {
        snprintf(error, error_size, "%s", L("cli.config_name_missing"));
        return 0;
    }
    return 1;
}

void cliPrintVersion(void) {
    printf("%s %s\n", brandLowerName(), EDITOR_VERSION);
}

void cliPrintHelp(void) {
    printf("%s", L("cli.help"));
}

int cliCleanLogs(void) {
    char directory[MAX_PATH];
    char pattern[MAX_PATH];
    if (!editorExecutableDirectory(directory, sizeof(directory)) ||
        snprintf(pattern, sizeof(pattern), "%s%s\\*.log", directory,
                 EDITOR_LOGS_DIR) >=
            (int)sizeof(pattern)) {
        fprintf(stderr, "%s: error: %s\n", brandLowerName(),
                L("cli.log_directory"));
        return 0;
    }

    WIN32_FIND_DATAA data;
    HANDLE search = FindFirstFileA(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            printf(L("cli.logs_cleared"), 0);
            return 1;
        }
        fprintf(stderr, L("cli.log_read_failed"),
                error);
        return 0;
    }

    int removed = 0, failed = 0;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char path[MAX_PATH];
        if (snprintf(path, sizeof(path), "%s%s\\%s",
                     directory, EDITOR_LOGS_DIR, data.cFileName) >=
                (int)sizeof(path) ||
            !DeleteFileA(path)) {
            failed++;
        } else {
            removed++;
        }
    } while (FindNextFileA(search, &data));
    FindClose(search);

    if (failed) {
        fprintf(stderr, L("cli.log_delete_failed"),
                removed, failed);
        return 0;
    }
    printf(L("cli.logs_cleared"), removed);
    return 1;
}

static int validConfigName(const char *name) {
    if (!name || !name[0]) return 0;
    for (const unsigned char *at = (const unsigned char *)name; *at; at++)
        if (!isalnum(*at) && *at != '-' && *at != '_') return 0;
    return 1;
}

static int hasIniSuffix(const char *name) {
    size_t length = strlen(name);
    return length >= 4 && _stricmp(name + length - 4, ".ini") == 0;
}

static int compareNames(const void *left, const void *right) {
    const char *const *a = left;
    const char *const *b = right;
    return _stricmp(*a, *b);
}

static void appendName(char ***names, int *count, int *capacity,
                       const char *name) {
    if (*count >= *capacity) {
        *capacity = *capacity ? *capacity * 2 : 8;
        *names = realloc(*names, sizeof(char *) * *capacity);
        if (!*names) loggerFatal("%s", L("cli.config_list_allocation"));
    }
    (*names)[(*count)++] = editor_strdup(name);
}

static char **listIniNames(const char *relative_pattern,
                           const char *label, int *count) {
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA data;
    HANDLE search;
    char **names = NULL;
    int capacity = 0;
    *count = 0;

    if (!editorExecutablePath(relative_pattern, pattern, sizeof(pattern)))
        loggerFatal(L("cli.config_directory"), label);
    search = FindFirstFileA(pattern, &data);
    if (search == INVALID_HANDLE_VALUE)
        loggerFatal(L("cli.no_ini_files"), label);

    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!hasIniSuffix(data.cFileName)) continue;
        size_t length = strlen(data.cFileName) - 4;
        char name[MAX_PATH];
        if (length == 0 || length >= sizeof(name)) continue;
        memcpy(name, data.cFileName, length);
        name[length] = '\0';
        if (!validConfigName(name)) {
            loggerLog(LOG_WARN, L("log.invalid_config_ignored"), data.cFileName);
            continue;
        }
        appendName(&names, count, &capacity, name);
    } while (FindNextFileA(search, &data));
    FindClose(search);

    if (*count == 0) {
        free(names);
        loggerFatal(L("cli.no_valid_ini_files"), label);
    }
    qsort(names, *count, sizeof(char *), compareNames);
    return names;
}

static char *chooseName(char **names, int count, const char *prompt) {
    int selected = 0;
    int name_width = 0;
    for (int i = 0; i < count; i++) {
        int width = (int)strlen(names[i]);
        if (width > name_width) name_width = width;
    }

    printf(L("cli.choose_hint"), prompt);
    printf("\033[?25l");
    for (;;) {
        for (int i = 0; i < count; i++) {
            const char *color = i == selected ? "\033[97m" : "\033[90m";
            printf("%s%c %-*s\033[0m\033[K\n", color,
                   i == selected ? '>' : ' ', name_width, names[i]);
        }
        fflush(stdout);

        int key = _getch();
        if (key == 0 || key == 224) {
            key = _getch();
            if (key == 72) selected = (selected - 1 + count) % count;
            else if (key == 80) selected = (selected + 1) % count;
            else continue;
        } else if (key == 13) {
            break;
        } else if (key == 27) {
            printf("\033[?25h");
            for (int i = 0; i < count; i++) free(names[i]);
            free(names);
            loggerLog(LOG_INFO, "%s", L("log.config_choice_canceled"));
            return NULL;
        } else {
            continue;
        }
        printf("\033[%dA", count);
    }
    printf("\033[?25h");

    char *result = editor_strdup(names[selected]);
    for (int i = 0; i < count; i++) free(names[i]);
    free(names);
    loggerLog(LOG_INFO, L("log.config_chosen"), result);
    return result;
}

static char *interactiveLanguageName(void) {
    int count;
    char **names = listIniNames(EDITOR_LANGUAGES_DIR "\\*.ini",
                                "lang", &count);
    char *language = chooseName(names, count, L("cli.choose_language"));
    if (!language) return NULL;
    char combined[MAX_PATH];
    if (snprintf(combined, sizeof(combined), "lang=%s", language) >=
        (int)sizeof(combined)) {
        free(language);
        loggerFatal("%s", L("cli.language_name_long"));
    }
    free(language);
    return editor_strdup(combined);
}

static char *interactiveConfigName(void) {
    int count;
    char **names = listIniNames("config\\*.ini", "config", &count);
    int capacity = count;
    char lang_directory[MAX_PATH];
    if (editorExecutablePath(EDITOR_LANGUAGES_DIR, lang_directory,
                             sizeof(lang_directory))) {
        DWORD attributes = GetFileAttributesA(lang_directory);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY))
            appendName(&names, &count, &capacity, "lang");
    }
    qsort(names, count, sizeof(char *), compareNames);
    char *result = chooseName(names, count, L("cli.choose_config"));
    if (result && _stricmp(result, "lang") == 0) {
        free(result);
        return interactiveLanguageName();
    }
    return result;
}

char *cliResolveConfigFile(const char *configured_name) {
    char *owned_name = NULL;
    const char *name = configured_name;
    char relative[MAX_PATH];
    char path[MAX_PATH];

    if (!name) {
        owned_name = interactiveConfigName();
        if (!owned_name) return NULL;
        name = owned_name;
    }
    if (_stricmp(name, "lang") == 0) {
        free(owned_name);
        owned_name = interactiveLanguageName();
        if (!owned_name) return NULL;
        name = owned_name;
    }
    if (hasIniSuffix(name)) {
        loggerFatal(L("cli.ini_suffix_forbidden"), name);
    }
    const char *language_name = NULL;
    if (_strnicmp(name, "lang=", 5) == 0) language_name = name + 5;
    if (language_name) {
        if (!validConfigName(language_name))
            loggerFatal(L("cli.invalid_language_name"), language_name);
        if (snprintf(relative, sizeof(relative),
                     EDITOR_LANGUAGES_DIR "\\%s.ini",
                     language_name) >= (int)sizeof(relative))
            loggerFatal(L("cli.config_path_long"), name);
    } else {
        if (!validConfigName(name))
            loggerFatal(L("cli.invalid_config_name"),
                        name);
        if (snprintf(relative, sizeof(relative), "config\\%s.ini", name) >=
            (int)sizeof(relative))
            loggerFatal(L("cli.config_path_long"), name);
    }
    if (!editorExecutablePath(relative, path, sizeof(path))) {
        loggerFatal(L("cli.config_path_long"), name);
    }
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        loggerFatal(L("cli.config_missing"), name);
    }
    loggerLog(LOG_INFO, L("log.edit_config"), path);
    free(owned_name);
    return editor_strdup(path);
}
