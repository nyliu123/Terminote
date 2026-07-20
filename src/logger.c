#include "common.h"
#include "logger.h"
#include "paths.h"
#include "localization.h"

static FILE *log_file = NULL;
static int log_verbose = 0;
static char log_path[MAX_PATH] = "";
static char logs_directory[MAX_PATH] = "";
static char *pending_log = NULL;
static size_t pending_length = 0;
static size_t pending_capacity = 0;

#define PENDING_LOG_LIMIT (256u * 1024u)

static const char *levelName(enum logLevel level) {
    static const char *names[] = {
        "log.level_debug", "log.level_info", "log.level_pass",
        "log.level_warn", "log.level_error"
    };
    if (level < LOG_DEBUG || level > LOG_ERROR) return L("log.level_info");
    return L(names[level]);
}

static const char *levelColor(enum logLevel level) {
    static const char *colors[] = {
        "\033[90m", "\033[96m", "\033[92m", "\033[93m", "\033[91m"
    };
    if (level < LOG_DEBUG || level > LOG_ERROR) return "\033[0m";
    return colors[level];
}

static void enableConsoleColors(void) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (output == INVALID_HANDLE_VALUE || !GetConsoleMode(output, &mode)) return;
    SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

static void appendPendingLine(const char *line) {
    size_t length = strlen(line);
    if (length + 1 >= PENDING_LOG_LIMIT) return;
    if (pending_length + length + 1 > PENDING_LOG_LIMIT) {
        size_t drop = pending_length + length + 1 - PENDING_LOG_LIMIT;
        while (drop < pending_length && pending_log[drop - 1] != '\n') drop++;
        memmove(pending_log, pending_log + drop, pending_length - drop);
        pending_length -= drop;
    }
    size_t required = pending_length + length + 2;
    if (required > pending_capacity) {
        size_t capacity = pending_capacity ? pending_capacity * 2 : 4096;
        while (capacity < required) capacity *= 2;
        char *grown = realloc(pending_log, capacity);
        if (!grown) return;
        pending_log = grown;
        pending_capacity = capacity;
    }
    memcpy(pending_log + pending_length, line, length);
    pending_length += length;
    pending_log[pending_length++] = '\n';
    pending_log[pending_length] = '\0';
}

static int openLogFile(void) {
    SYSTEMTIME now;
    if (log_file) return 1;
    if (!logs_directory[0]) return 0;
    if (!CreateDirectoryA(logs_directory, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
        return 0;

    GetLocalTime(&now);
    if (snprintf(log_path, sizeof(log_path),
                 "%s\\%04u%02u%02u-%02u%02u%02u-%03u.log",
                 logs_directory, now.wYear, now.wMonth, now.wDay,
                 now.wHour, now.wMinute, now.wSecond, now.wMilliseconds) >=
        (int)sizeof(log_path)) {
        log_path[0] = '\0';
        return 0;
    }

    log_file = fopen(log_path, "wb");
    if (!log_file) {
        log_path[0] = '\0';
        return 0;
    }
    if (pending_log && pending_length > 0)
        fwrite(pending_log, 1, pending_length, log_file);
    free(pending_log);
    pending_log = NULL;
    pending_length = 0;
    pending_capacity = 0;
    return 1;
}

int loggerInitialize(int verbose) {
    char directory[MAX_PATH];

    log_verbose = verbose;
    enableConsoleColors();
    if (!editorExecutableDirectory(directory, sizeof(directory))) return 0;
    if (snprintf(logs_directory, sizeof(logs_directory), "%s%s", directory,
                 EDITOR_LOGS_DIR) >=
        (int)sizeof(logs_directory)) {
        logs_directory[0] = '\0';
        return 0;
    }
    log_path[0] = '\0';
    atexit(loggerClose);
    return 1;
}

void loggerLog(enum logLevel level, const char *format, ...) {
    SYSTEMTIME now;
    char message[768];
    char line[1024];
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    GetLocalTime(&now);
    snprintf(line, sizeof(line),
             "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%s] %s",
             now.wYear, now.wMonth, now.wDay,
             now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
             levelName(level), message);

    if (!log_file && level >= LOG_WARN) openLogFile();
    if (log_file) {
        fprintf(log_file, "%s\n", line);
        fflush(log_file);
    } else if (level < LOG_WARN) {
        appendPendingLine(line);
    }

    if (log_verbose) {
        fprintf(stdout, "%s%s\033[0m\n", levelColor(level), line);
        fflush(stdout);
    } else if (level == LOG_WARN) {
        fprintf(stderr, "\033[93m");
        fprintf(stderr, L("log.warning_output"), message);
        fprintf(stderr, "\033[0m\n");
        fflush(stderr);
    } else if (level == LOG_ERROR) {
        fprintf(stderr, "\033[91m");
        fprintf(stderr, L("log.error_output"), message);
        fprintf(stderr, "\033[0m\n");
        fflush(stderr);
    }
}

_Noreturn void loggerFatal(const char *format, ...) {
    char message[768];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    loggerLog(LOG_ERROR, "%s", message);
    loggerClose();
    exit(1);
}

void loggerClose(void) {
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    free(pending_log);
    pending_log = NULL;
    pending_length = 0;
    pending_capacity = 0;
}

int loggerIsVerbose(void) {
    return log_verbose;
}

const char *loggerFilePath(void) {
    return log_path;
}
