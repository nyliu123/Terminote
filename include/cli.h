#ifndef CLI_H
#define CLI_H

struct cliOptions {
    int verbose;
    int config_mode;
    const char *config_name;
    const char *filenames[64];
    int file_count;
};

int cliHasVersionOption(int argc, char **argv);
int cliHasHelpOption(int argc, char **argv);
int cliHasCleanOption(int argc, char **argv);
int cliWantsVerbose(int argc, char **argv);
int cliParse(int argc, char **argv, struct cliOptions *options,
             char *error, size_t error_size);
void cliPrintHelp(void);
void cliPrintVersion(void);
int cliCleanLogs(void);
char *cliResolveConfigFile(const char *configured_name);

#endif
