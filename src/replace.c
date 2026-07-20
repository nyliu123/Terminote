#include "common.h"
#include "terminal.h"
#include "row.h"
#include "undo.h"
#include "output.h"
#include "editor.h"
#include "prompt.h"
#include "replace.h"
#include "keybindings.h"
#include "mouse.h"
#include "search.h"
#include "localization.h"

static char *replacePrompt(const char *label) {
    char prompt[128];
    snprintf(prompt, sizeof(prompt), L("replace.prompt"), label,
             editorBindingLabel(ACTION_INSERT_NEWLINE));
    return editorSearchPrompt(prompt, NULL);
}

static int confirmReplacement(struct searchResults *results, int *index,
                              int query_length, int *replace_all,
                              int *skipped) {
    int decision = 0;
    E.replace_confirm_active = 1;
    while (1) {
        struct findMatch *match = &results->items[*index];
        char *highlight = editorSearchHighlight(match, query_length);
        editorSetStatusMessage(
            L("replace.confirm"),
            *index + 1, results->count,
            editorBindingLabel(ACTION_REPLACE_YES),
            editorBindingLabel(ACTION_REPLACE_NO),
            editorBindingLabel(ACTION_REPLACE_ALL),
            editorBindingLabel(ACTION_MOVE_UP),
            editorBindingLabel(ACTION_MOVE_DOWN),
            editorBindingLabel(ACTION_CANCEL));
        editorRefreshScreen();
        int key = editorReadKey();
        editorSearchRestoreHighlight(match, highlight);

        if (key == ACTION_MOVE_DOWN || key == ACTION_MOVE_UP) {
            int step = key == ACTION_MOVE_DOWN ? 1 : results->count - 1;
            *index = (*index + step) % results->count;
            editorSearchShowMatch(&results->items[*index], query_length, 1);
            continue;
        }
        if (editorHandleModalViewInput(key, 1)) continue;
        if (key == ACTION_REPLACE_NO) { (*skipped)++; break; }
        if (key == ACTION_REPLACE_ALL) {
            *replace_all = decision = 1;
            break;
        }
        if (key == ACTION_REPLACE_YES) { decision = 1; break; }
        if (key == ACTION_CANCEL || key == MOUSE_PRESS) {
            E.replace_confirm_active = 0;
            return -1;
        }
    }
    E.replace_confirm_active = 0;
    return decision;
}

static void replaceOne(const struct findMatch *match, int query_length,
                       const char *replacement, int replacement_length) {
    int old_length;
    char *old_text = editorGetTextInRange(
        match->row, match->col, match->row, match->col + query_length,
        &old_length);
    erow *row = &E.row[match->row];

    undo_suspended++;
    for (int i = 0; i < query_length; i++) editorRowDelChar(row, match->col);
    for (int i = 0; i < replacement_length; i++)
        editorRowInsertChar(row, match->col + i,
                            (unsigned char)replacement[i]);
    if (undo_suspended > 0) undo_suspended--;

    editorPushUndoAtomicReplace(
        match->row, match->col, match->row, match->col + query_length,
        match->row, match->col + replacement_length, match->row, match->col,
        old_text, old_length, editor_strdup(replacement), replacement_length);
}

void editorReplace(void) {
    struct searchResults results = {0};
    E.search_active = 1;
    char *query = replacePrompt(L("replace.find"));
    if (!query || !query[0]) {
        free(query);
        E.search_active = 0;
        editorSetStatusMessage("");
        return;
    }

    editorSearchCollect(&results, query);
    if (!results.count) {
        E.search_active = 0;
        editorSetStatusMessage(L("find.not_found"), query);
        editorSearchFree(&results);
        free(query);
        return;
    }

    char *replacement = replacePrompt(L("replace.with"));
    if (!replacement) {
        editorSearchFree(&results);
        free(query);
        E.search_active = 0;
        editorSetStatusMessage("");
        return;
    }

    int query_length = (int)strlen(query);
    int replacement_length = (int)strlen(replacement);
    int index = editorSearchIndexAtOrAfter(&results, E.cy, E.cx);
    int replaced = 0, replace_all = 0, skipped = 0, canceled = 0;

    while (index < results.count) {
        editorSearchShowMatch(&results.items[index], query_length, 0);
        if (!replace_all) {
            int decision = confirmReplacement(&results, &index, query_length,
                                              &replace_all, &skipped);
            if (decision < 0) { canceled = 1; break; }
            if (!replace_all && decision == 0) { index++; continue; }
        }

        struct findMatch current = results.items[index];
        replaceOne(&current, query_length, replacement, replacement_length);
        replaced++;

        int next_row = current.row;
        int next_col = current.col + replacement_length;
        editorSearchCollect(&results, query);
        index = editorSearchIndexAtOrAfter(&results, next_row, next_col);
    }

    editorSearchFree(&results);
    free(query);
    free(replacement);
    E.search_active = 0;
    editorSetStatusMessage(canceled ? "" : L("replace.completed"), replaced,
                           skipped ? L("replace.skipped") : "");
}
