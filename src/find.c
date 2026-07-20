#include "common.h"
#include "terminal.h"
#include "output.h"
#include "prompt.h"
#include "find.h"
#include "keybindings.h"
#include "mouse.h"
#include "search.h"
#include "localization.h"

struct savedView {
    int cx, cy;
    int coloff, rowoff;
};

static void restoreView(const struct savedView *view) {
    E.cx = view->cx;
    E.cy = view->cy;
    E.coloff = view->coloff;
    E.rowoff = view->rowoff;
}

static int findInput(int key, int *index, int count) {
    if (key == ACTION_MOVE_DOWN || key == ACTION_MOVE_UP) {
        *index = (*index + (key == ACTION_MOVE_DOWN ? 1 : count - 1)) % count;
        return 1;
    }
    if (key == ACTION_CANCEL) return -1;
    if (editorHandleModalViewInput(key, 1)) return 0;
    return key == MOUSE_PRESS ? -1 : 0;
}

void editorFind(void) {
    struct savedView initial = {E.cx, E.cy, E.coloff, E.rowoff};
    struct searchResults results = {0};
    E.search_active = 1;

    char prompt[128];
    snprintf(prompt, sizeof(prompt), L("find.prompt"),
             editorBindingLabel(ACTION_INSERT_NEWLINE));
    char *query = editorSearchPrompt(prompt, NULL);
    if (!query || !query[0]) {
        free(query);
        restoreView(&initial);
        E.search_active = 0;
        editorSetStatusMessage("");
        return;
    }

    int query_length = (int)strlen(query);
    editorSearchCollect(&results, query);
    if (!results.count) {
        E.search_active = 0;
        editorSetStatusMessage(L("find.not_found"), query);
        editorSearchFree(&results);
        free(query);
        return;
    }

    int index = editorSearchIndexAtOrAfter(&results, E.cy, E.cx);
    int reposition = 1;
    while (1) {
        struct findMatch *match = &results.items[index];
        if (reposition) editorSearchShowMatch(match, query_length, 1);
        reposition = 0;

        char *highlight = editorSearchHighlight(match, query_length);
        editorSetStatusMessage(L("find.current"),
                               query, index + 1, results.count,
                               editorBindingLabel(ACTION_MOVE_UP),
                               editorBindingLabel(ACTION_MOVE_DOWN),
                               editorBindingLabel(ACTION_CANCEL));
        editorRefreshScreen();
        int action = findInput(editorReadKey(), &index, results.count);
        editorSearchRestoreHighlight(match, highlight);
        if (action < 0) break;
        reposition = action;
    }

    editorSearchFree(&results);
    free(query);
    E.search_active = 0;
    editorSetStatusMessage("");
}
