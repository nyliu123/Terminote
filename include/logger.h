#ifndef LOGGER_H
#define LOGGER_H

enum logLevel {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_PASS,
    LOG_WARN,
    LOG_ERROR
};

int loggerInitialize(int verbose);
void loggerLog(enum logLevel level, const char *format, ...);
_Noreturn void loggerFatal(const char *format, ...);
void loggerClose(void);
int loggerIsVerbose(void);
const char *loggerFilePath(void);

#endif
