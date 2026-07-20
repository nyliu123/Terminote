#include "common.h"
#include "terminal.h"
#include "undo.h"
#include "output.h"
#include "file.h"
#include "find.h"
#include "replace.h"
#include "editor.h"
#include "mouse.h"
#include "completion.h"
#include "tabs.h"
#include "execution.h"
#include "settings.h"
#include "bracket.h"
#include "localization.h"
#include "restart.h"

static int editorApplyAtomicPaste(char *text, int length) {
    int had_selection = E.sel_active;
    int old_sr = 0, old_sc = 0, old_er = 0, old_ec = 0;
    int saved_row = E.cy, saved_col = E.cx;
    int old_length = 0;
    int character_count = 0;
    char *old_text = NULL;

    if (!text || length <= 0) {
        free(text);
        return 0;
    }

    if (had_selection) {
        old_sr = E.sel_anchor_row; old_sc = E.sel_anchor_col;
        old_er = E.cy;             old_ec = E.cx;
        if (old_er < old_sr || (old_er == old_sr && old_ec < old_sc)) {
            int swap = old_sr; old_sr = old_er; old_er = swap;
            swap = old_sc; old_sc = old_ec; old_ec = swap;
        }
        old_text = editorGetTextInRange(old_sr, old_sc, old_er, old_ec,
                                        &old_length);
    }

    for (int i = 0; i < length; character_count++) {
        if (text[i] == '\r' && i + 1 < length && text[i + 1] == '\n') i += 2;
        else i++;
    }

    undo_suspended++;
    if (E.sel_active) editorDeleteSelection();
    int insert_row = E.cy, insert_col = E.cx;
    editorInsertBlock(text, length);
    if (undo_suspended > 0) undo_suspended--;

    if (had_selection && old_text) {
        editorPushUndoAtomicReplace(old_sr, old_sc, old_er, old_ec,
                                    E.cy, E.cx, saved_row, saved_col,
                                    old_text, old_length, text, length);
    } else {
        editorPushUndoBlock(UO_ATOMIC_INSERT,
                            insert_row, insert_col, E.cy, E.cx,
                            saved_row, saved_col, text, length);
        free(old_text);
    }
    editorSuppressBracketMatchAtCursor();
    return character_count;
}

static char *editorReadClipboardText(int *length) {
    char *text = NULL;
    *length = 0;
    if (!OpenClipboard(NULL)) return NULL;

    HANDLE data = GetClipboardData(CF_TEXT);
    if (data) {
        char *source = (char *)GlobalLock(data);
        if (source) {
            *length = (int)strlen(source);
            text = malloc(*length + 1);
            if (!text) die("malloc: clipboard text");
            memcpy(text, source, *length + 1);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

static void editorCopySelection(void) {
    int length;
    char *selection = editorGetSelectedText(&length);
    if (!selection || length <= 0) {
        editorSetStatusMessage("%s", L("selection.none"));
        free(selection);
        return;
    }

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, length + 1);
        if (memory) {
            char *target = (char *)GlobalLock(memory);
            memcpy(target, selection, length);
            target[length] = '\0';
            GlobalUnlock(memory);
            SetClipboardData(CF_TEXT, memory);
        }
        CloseClipboard();
    }
    editorSetStatusMessage("%s", L("selection.copied"));
    editorSetEvent(L("selection.copied_count"), length);
    free(selection);
}

static void editorSelectAll(void) {
    E.wheel_scrolling = 0;
    if (E.numrows <= 0) return;
    E.sel_anchor_row = 0;
    E.sel_anchor_col = 0;
    E.sel_active = 1;
    E.cy = E.numrows - 1;
    E.cx = E.row[E.numrows - 1].size;
    editorSetStatusMessage("%s", L("selection.all"));
}

static void editorMovePage(int key) {
    int page_size = editorVisibleRows();
    if (page_size < 1) page_size = 1;
    if (key == ACTION_PAGE_UP) {
        E.cy = E.rowoff;
    } else {
        E.cy = E.rowoff + page_size - 1;
        if (E.cy >= E.numrows && E.numrows > 0) E.cy = E.numrows - 1;
    }
    for (int count = page_size; count-- > 0;)
        editorMoveCursor(key == ACTION_PAGE_UP ? ACTION_MOVE_UP
                                               : ACTION_MOVE_DOWN);
}

void editorProcessKeypress(void) {
    int key = editorReadKeyNonBlocking();
    if (key == NO_KEY_EVENT) return;
    int typed_character = key >= 32 && key <= 255 && key != 127;
    if (!typed_character && key != WINDOW_RESIZED && key != MOUSE_HOVER)
        editorEndUndoGroup();
    if (editorHandleSettingsInput(key)) return;
    if ((key == WHEEL_UP || key == WHEEL_DOWN || key == MOUSE_PRESS ||
         key == MOUSE_DRAG || key == MOUSE_RELEASE || key == MOUSE_CLICK ||
         key == MOUSE_HOVER) &&
        editorHandleTabMouseEvent(key))
        return;
    if (key != WINDOW_RESIZED && key != MOUSE_HOVER) {
        E.statusmsg[0] = '\0';
        E.cur_event[0] = '\0';
    }
    if (editorHandleCompletionInput(key)) return;
    if (key != WINDOW_RESIZED && key != MOUSE_HOVER)
        editorResetCompletion();

    switch (key) {
        case ACTION_INSERT_NEWLINE:
            if (E.sel_active) editorDeleteSelection();
            editorInsertNewlineWithIndent();
            editorSetEvent("%s", L("edit.newline"));
            break;
        case ACTION_QUIT_WITHOUT_SAVING:
            if (editorCloseCurrentTab()) break;
            SetConsoleTextAttribute(E.hout, 7);
            system("cls");
            exit(0);
        case ACTION_CUT:
            editorCutSelection();
            editorSetEvent("%s", L("selection.cut_event"));
            break;
        case ACTION_SAVE_AS:
            if (editorSaveAs()) {
                editorSetEvent(L("file.saved_as"), E.filename);
                editorRestartAfterProjectConfigSave();
            }
            break;
        case ACTION_OPEN: {
            char *path = editorChooseOpenPath();
            if (path) {
                editorTabsOpenPath(path);
                free(path);
            }
            break;
        }
        case ACTION_NEW_FILE:
            editorTabsNewUntitled();
            break;
        case ACTION_NEXT_TAB:
            editorNextTab();
            break;
        case ACTION_RUN:
            editorRunCurrentFile();
            break;
        case ACTION_SAVE:
            if (editorSave()) {
                editorSetEvent(L("file.saved_name"), E.filename);
                editorRestartAfterProjectConfigSave();
            }
            break;
        case ACTION_FIND:
            editorFind();
            break;
        case ACTION_FIND_AND_REPLACE:
            editorReplace();
            break;
        case ACTION_UNDO:
            editorUndo();
            editorUpdateCompletion();
            break;
        case ACTION_REDO:
            editorRedo();
            editorUpdateCompletion();
            break;
        case ACTION_MOVE_TO_LINE_START:
            E.wheel_scrolling = 0;
            E.cx = 0;
            editorSetEvent("%s", L("edit.cursor_move"));
            break;
        case ACTION_MOVE_TO_LINE_END:
            E.wheel_scrolling = 0;
            if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
            editorSetEvent("%s", L("edit.cursor_move"));
            break;
        case ACTION_HELP:
            editorHelp();
            break;
        case ACTION_DELETE_BACKWARD:
        case ACTION_DELETE_FORWARD:
            if (E.sel_active) editorDeleteSelection();
            else {
                if (key == ACTION_DELETE_FORWARD) {
                    editorMoveCursor(ACTION_MOVE_RIGHT);
                    editorDelChar();
                } else if (!editorDeleteAutoPair()) {
                    editorDelChar();
                }
            }
            editorSetEvent("%s", L("edit.delete"));
            editorUpdateCompletion();
            break;
        case ACTION_PAGE_UP:
        case ACTION_PAGE_DOWN:
            editorMovePage(key);
            editorSetEvent("%s", L("edit.page"));
            break;
        case ACTION_MOVE_UP:
        case ACTION_MOVE_DOWN:
        case ACTION_MOVE_LEFT:
        case ACTION_MOVE_RIGHT:
            editorMoveCursor(key);
            editorSetEvent("%s", L("edit.cursor_move"));
            break;
        case ACTION_PLACEHOLDER:
        case ACTION_CANCEL:
            editorSetEvent("%s", L("edit.cancel"));
            break;
        case ACTION_INDENT:
            if (E.sel_active) {
                editorIndentSelection();
                break;
            }
            editorInsertConfiguredTab();
            editorSetEvent(L("edit.indent"), E.tab_width);
            break;
        case ACTION_UNINDENT:
            if (E.sel_active) editorUnindentSelection();
            break;
        case ACTION_SELECT_ALL:
            editorSelectAll();
            break;
        case ACTION_COPY:
            editorCopySelection();
            break;
        case PASTE_EVENT: {
            int length = 0;
            char *text = editorTakePendingPaste(&length);
            int count = editorApplyAtomicPaste(text, length);
            editorSetStatusMessage(L("edit.pasted_count"), count);
            editorSetEvent("%s", L("edit.atomic_paste"));
            editorUpdateCompletion();
            break;
        }
        case ACTION_PASTE: {
            int length;
            char *text = editorReadClipboardText(&length);
            int count = editorApplyAtomicPaste(text, length);
            if (count > 0) editorSetStatusMessage(L("edit.pasted_count"), count);
            else editorSetStatusMessage("%s", L("edit.pasted"));
            editorUpdateCompletion();
            break;
        }
        case WHEEL_UP:
        case WHEEL_DOWN:
        case MOUSE_PRESS:
        case MOUSE_DRAG:
        case MOUSE_RELEASE:
        case MOUSE_CLICK:
            editorProcessMouseEvent(key);
            break;
        case WINDOW_RESIZED:
            break;
        default:
            if (key >= 32 && key <= 255 && key != 127) {
                editorBeginTypingUndoGroup();
                if (E.sel_active) editorDeleteSelection();
                if (!editorInsertAutoPair(key)) editorInsertChar(key);
                editorSetEvent("%s", L("edit.character"));
                editorUpdateCompletion();
            }
            break;
    }
}
