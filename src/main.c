#include "common.h"
#include "terminal.h"
#include "row.h"
#include "editor.h"
#include "output.h"
#include "file.h"
#include "undo.h"
#include "keybindings.h"
#include "logger.h"
#include "preferences.h"
#include "cli.h"
#include "syntax.h"
#include "tabs.h"
#include "execution.h"
#include "localization.h"
#include "restart.h"
#include "brand.h"
#include "win_text.h"
#include <shellapi.h>

static char **utf8_arguments;
static int utf8_argument_count;

static void freeUtf8Arguments(void) {
    for (int i = 0; i < utf8_argument_count; i++) free(utf8_arguments[i]);
    free(utf8_arguments);
}

void initEditor(void) {
    E.cx = 0; E.cy = 0; E.rx = 0;
    E.rowoff = 0; E.coloff = 0;
    E.numrows = 0; E.row = NULL;
    E.dirty = 0; E.filename = NULL;
    E.save_failed = 0;
    E.statusmsg[0] = '\0';
    E.prompt_active = 0;
    E.search_active = 0;
    E.replace_confirm_active = 0;
    E.cur_event[0] = '\0'; E.input_activity_deadline = 0;
    E.linenum_width = 3;
    E.sel_anchor_row = 0; E.sel_anchor_col = 0;
    E.sel_active = 0; E.mouse_selecting = 0; E.wheel_scrolling = 0;
    E.hscroll_dragging = 0;
    E.auto_scroll_h = 0; E.auto_scroll_v = 0; E.mouse_drag_move = 0;
    E.drag_text = NULL; E.drag_text_len = 0;

    free(undo_stack); undo_stack = NULL;
    free(redo_stack); redo_stack = NULL;
    undo_count = undo_cap = redo_count = redo_cap = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        E.screenrows = 24; E.screencols = 80;
    }
    E.screenrows -= 3;  /* tabs + event + status bars */

    /* park window at origin then shrink buffer to match --       hide the native vertical scrollbar */
    editorFitBufferToWindow();
    hideConsoleScrollbars();
}

static int cliEarlyExit(int argc, char **argv, int *status) {
    if (cliHasCleanOption(argc, argv)) {
        if (argc != 2 || strcmp(argv[1], "--clean") != 0) {
            fprintf(stderr, "%s: error: %s\n", brandLowerName(),
                    L("cli.clean_with_options"));
            *status = 2;
        } else {
            *status = cliCleanLogs() ? 0 : 1;
        }
        return 1;
    }
    if (cliHasVersionOption(argc, argv)) cliPrintVersion();
    else if (cliHasHelpOption(argc, argv)) cliPrintHelp();
    else return 0;
    *status = 0;
    return 1;
}

static void editorOpenInitialFiles(const struct cliOptions *options,
                                   char *config_file) {
    if (config_file) {
        editorOpen(config_file);
        free(config_file);
    } else if (options->file_count > 0) {
        editorOpen(options->filenames[0]);
        editorTabsSyncCurrent();
        for (int i = 1; i < options->file_count; i++)
            editorTabsOpenPath(options->filenames[i]);
    }
    editorTabsSyncCurrent();
}

static void editorContinueMouseAutoScroll(void) {
    if (!E.mouse_selecting || (!E.auto_scroll_h && !E.auto_scroll_v)) return;

    if (E.auto_scroll_v < 0 && E.rowoff > 0) E.rowoff--;
    if (E.auto_scroll_v > 0) E.rowoff++;
    if (E.auto_scroll_h < 0 && E.coloff > 0) E.coloff--;
    if (E.auto_scroll_h > 0) E.coloff++;

    int row = E.mouse_y - 1 + E.rowoff;
    if (row >= E.numrows) row = E.numrows > 0 ? E.numrows - 1 : 0;
    if (row < 0) row = 0;
    E.cy = row;
    if (row >= E.numrows) return;

    int text_width = E.screencols - E.linenum_width - 1;
    if (text_width < 1) text_width = 1;
    int x = E.mouse_x - E.linenum_width - 1;
    if (x < 0) x = 0;
    if (x >= text_width) x = text_width - 1;
    E.cx = editorRowRxToCx(&E.row[row], x + E.coloff);
}

int main(int argc, char *argv[]) {
    /* CRT narrow argv uses the system ANSI code page, not the console CP. */
    wchar_t **wide_arguments = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide_arguments) return 1;
    utf8_arguments = calloc(argc, sizeof(*utf8_arguments));
    if (!utf8_arguments) { LocalFree(wide_arguments); return 1; }
    utf8_argument_count = argc;
    atexit(freeUtf8Arguments);
    for (int i = 0; i < argc; i++) {
        utf8_arguments[i] = editorWideToUtf8(wide_arguments[i]);
        if (!utf8_arguments[i]) { LocalFree(wide_arguments); return 1; }
    }
    LocalFree(wide_arguments);
    argv = utf8_arguments;
    struct cliOptions options;
    char cli_error[256];
    char *config_file = NULL;
    char restart_manifest[MAX_PATH];

    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    brandInitialize();
    localizationInitialize();
    int restart_state = editorRestartSessionRequested(
        restart_manifest, sizeof(restart_manifest));
    if (restart_state < 0) return 1;
    int restart_requested = restart_state > 0;

    int status;
    if (cliEarlyExit(argc, argv, &status)) return status;

    int verbose = cliWantsVerbose(argc, argv);
    if (!loggerInitialize(verbose)) {
        fprintf(stderr, "%s: error: %s\n", brandLowerName(),
                L("cli.logger_initialize_failed"));
        return 1;
    }
    if (!cliParse(argc, argv, &options, cli_error, sizeof(cli_error))) {
        loggerLog(LOG_ERROR, "%s", cli_error);
        if (!verbose)
            fprintf(stderr, "%s: note: %s\n", brandLowerName(),
                    L("cli.help_note"));
        return 2;
    }

    loggerLog(LOG_INFO, L("log.startup"), EDITOR_VERSION);
    loggerLog(LOG_INFO, L("log.cli_parsed"),
              options.verbose, options.config_mode,
              options.file_count,
              options.file_count ? options.filenames[0] : L("log.none"));
    if (options.config_mode) {
        config_file = cliResolveConfigFile(options.config_name);
        if (!config_file) return 0;
    }

    editorLoadPreferences();
    if (config_file)
        editorSelectLanguageForFile(config_file);
    else if (options.file_count > 0)
        editorSelectLanguageForFile(options.filenames[0]);
    editorLoadKeyBindings();
    editorLoadExecutionCommands();
    loggerLog(LOG_INFO, "%s", L("log.startup_validated"));
    if (!restart_requested)
        restart_requested = editorFindPendingRestartSession(
            restart_manifest, sizeof(restart_manifest));
    if (verbose) {
        loggerLog(LOG_INFO, "%s", L("log.enter_editor"));
        Sleep(1000);
    }

    hideConsoleScrollbars();  /* hide before raw mode kicks in */
    enableRawMode();
    initEditor();
    editorTabsInitialize();

    if (restart_requested) {
        free(config_file);
        if (!editorTabsRestoreRestartSession(restart_manifest)) return 1;
        editorMarkRestartSessionRestored();
    } else {
        editorOpenInitialFiles(&options, config_file);
    }
    editorSignalRestartReady();

    while (1) {
        editorRefreshScreen();

        if (editorPollGlobalShortcut() == ACTION_NEXT_TAB)
            editorNextTab();

        DWORD result = WaitForSingleObject(E.hin, 50);
        if (result == WAIT_OBJECT_0) {
            editorProcessKeypress();
        } else editorContinueMouseAutoScroll();
    }

    return 0;
}
