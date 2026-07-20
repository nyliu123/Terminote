#include "common.h"
#include "completion.h"
#include "localization.h"
#include "editor.h"
#include "output.h"
#include "row.h"
#include "syntax.h"
#include "undo.h"
#include "colors.h"

#define COMPLETION_MAX_VISIBLE 8

struct completionState {
    char *prefix;
    int prefix_length;
    int row;
    int end_col;
    int selected;
    int count;
};

struct completionGeometry {
    int left;
    int top;
    int width;
    int visible;
    int first;
};

static struct completionState completion;
static int suppress_mouse_release = 0;

void editorResetCompletion(void) {
    free(completion.prefix);
    memset(&completion, 0, sizeof(completion));
}

int editorCompletionActive(void) {
    return completion.prefix != NULL && completion.count > 0;
}

static int isCompletionCharacter(unsigned char value) {
    return isalnum(value) || value == '_' || value == '#' ||
           value == '@' || value == '!';
}

static int textWidth(const char *text) {
    int width = 0;
    while (*text) {
        unsigned char value = (unsigned char)*text;
        if (value < 0x80) {
            text++;
            width++;
        } else {
            text += utf8_seq_len(value);
            width += 2;
        }
    }
    return width;
}

static int completionGeometry(struct completionGeometry *geometry) {
    if (!editorCompletionActive() || completion.row < E.rowoff ||
        completion.row >= E.rowoff + editorVisibleRows())
        return 0;

    int cursor_rx = editorRowCxToRx(&E.row[completion.row],
                                    completion.end_col);
    geometry->left = E.linenum_width + 1 + cursor_rx - E.coloff + 1;
    if (geometry->left < 0) geometry->left = 0;
    if (geometry->left >= E.screencols) return 0;

    int maximum_width = 0;
    for (int index = 0; index < completion.count; index++) {
        const char *candidate = editorSyntaxCompletionAt(
            completion.prefix, completion.prefix_length, index);
        int width = candidate ? textWidth(candidate) : 0;
        if (width > maximum_width) maximum_width = width;
    }
    geometry->width = maximum_width + 2;
    int available_width = E.screencols - geometry->left;
    if (geometry->width > available_width)
        geometry->width = available_width;
    if (geometry->width < 3) return 0;

    int content_rows = editorVisibleRows();
    geometry->visible = completion.count;
    if (geometry->visible > COMPLETION_MAX_VISIBLE)
        geometry->visible = COMPLETION_MAX_VISIBLE;
    if (geometry->visible > content_rows)
        geometry->visible = content_rows;
    if (geometry->visible <= 0) return 0;

    geometry->top = completion.row - E.rowoff + 1;
    if (geometry->top + geometry->visible > content_rows + 1)
        geometry->top = content_rows + 1 - geometry->visible;
    if (geometry->top < 1) geometry->top = 1;

    geometry->first = 0;
    if (completion.selected >= geometry->visible)
        geometry->first = completion.selected - geometry->visible + 1;
    if (geometry->first + geometry->visible > completion.count)
        geometry->first = completion.count - geometry->visible;
    return 1;
}

static void appendClippedCandidate(const char *candidate, int width) {
    int used = 0;
    while (*candidate && used < width) {
        unsigned char value = (unsigned char)*candidate;
        int length = value < 0x80 ? 1 : utf8_seq_len(value);
        int character_width = value < 0x80 ? 1 : 2;
        if (used + character_width > width) break;
        char character[5];
        memcpy(character, candidate, length);
        character[length] = '\0';
        abAppend(character);
        candidate += length;
        used += character_width;
    }
    while (used++ < width) abAppend(" ");
}

void editorDrawCompletionPopup(void) {
    struct completionGeometry geometry;
    if (!completionGeometry(&geometry)) return;

    for (int row = 0; row < geometry.visible; row++) {
        int index = geometry.first + row;
        const char *candidate = editorSyntaxCompletionAt(
            completion.prefix, completion.prefix_length, index);
        if (!candidate) continue;

        char position[32];
        snprintf(position, sizeof(position), "\x1b[%d;%dH",
                 geometry.top + row + 1, geometry.left + 1);
        abAppend(position);
        if (index == completion.selected) {
            abAppend(editorColor(COLOR_COMPLETION_SELECTED_BACKGROUND));
            abAppend(editorColor(COLOR_COMPLETION_SELECTED_TEXT));
        } else {
            abAppend(editorColor(COLOR_COMPLETION_BACKGROUND));
            abAppend(editorColor(COLOR_COMPLETION_TEXT));
        }
        abAppend(" ");
        appendClippedCandidate(candidate, geometry.width - 2);
        abAppend(" ");
        abAppend(editorColor(COLOR_RESET));
    }
}

void editorUpdateCompletion(void) {
    editorResetCompletion();
    if (!E.code_completion || !editorSyntaxEnabled() || E.sel_active ||
        E.prompt_active || E.search_active || E.replace_confirm_active ||
        E.cy < 0 || E.cy >= E.numrows)
        return;

    erow *row = &E.row[E.cy];
    if (E.cx <= 0 || E.cx > row->size) return;
    if (E.cx < row->size &&
        isCompletionCharacter((unsigned char)row->chars[E.cx]))
        return;

    int start = E.cx;
    while (start > 0 &&
           isCompletionCharacter((unsigned char)row->chars[start - 1]))
        start--;
    int prefix_length = E.cx - start;
    if (prefix_length <= 0) return;
    if (start > 0 && !isspace((unsigned char)row->chars[start - 1]))
        return;

    char *prefix = malloc(prefix_length + 1);
    if (!prefix) die("malloc: completion prefix");
    memcpy(prefix, &row->chars[start], prefix_length);
    prefix[prefix_length] = '\0';

    int count = editorSyntaxCompletionCount(prefix, prefix_length);
    if (count <= 0) {
        free(prefix);
        return;
    }

    completion.prefix = prefix;
    completion.prefix_length = prefix_length;
    completion.row = E.cy;
    completion.end_col = E.cx;
    completion.selected = 0;
    completion.count = count;
}

static void applySelectedCompletion(void) {
    const char *candidate = editorSyntaxCompletionAt(
        completion.prefix, completion.prefix_length, completion.selected);
    if (!candidate) {
        editorResetCompletion();
        return;
    }

    char completed_word[256];
    snprintf(completed_word, sizeof(completed_word), "%s", candidate);
    int candidate_length = (int)strlen(candidate);
    int suffix_length = candidate_length - completion.prefix_length;
    if (suffix_length <= 0) {
        editorResetCompletion();
        return;
    }

    char *suffix = malloc(suffix_length + 1);
    if (!suffix) die("malloc: completion suffix");
    memcpy(suffix, candidate + completion.prefix_length, suffix_length);
    suffix[suffix_length] = '\0';

    int start_row = E.cy;
    int start_col = E.cx;
    undo_suspended++;
    editorInsertBlock(suffix, suffix_length);
    if (undo_suspended > 0) undo_suspended--;
    editorPushUndoBlock(UO_ATOMIC_INSERT,
                        start_row, start_col, E.cy, E.cx,
                        start_row, start_col, suffix, suffix_length);

    editorResetCompletion();
    editorSetStatusMessage(L("completion.completed"), completed_word);
    editorSetEvent("%s", L("completion.event"));
}

static int completionIndexAtMouse(void) {
    struct completionGeometry geometry;
    if (!completionGeometry(&geometry)) return -1;
    if (E.mouse_x < geometry.left ||
        E.mouse_x >= geometry.left + geometry.width ||
        E.mouse_y < geometry.top ||
        E.mouse_y >= geometry.top + geometry.visible)
        return -1;
    return geometry.first + E.mouse_y - geometry.top;
}

int editorHandleCompletionInput(int key) {
    if (suppress_mouse_release && key == MOUSE_RELEASE) {
        suppress_mouse_release = 0;
        return 1;
    }
    if (!editorCompletionActive()) return 0;

    switch (key) {
        case ACTION_MOVE_UP:
            completion.selected--;
            if (completion.selected < 0)
                completion.selected = completion.count - 1;
            editorSetEvent(L("completion.select"),
                           completion.selected + 1, completion.count);
            return 1;
        case ACTION_MOVE_DOWN:
            completion.selected++;
            if (completion.selected >= completion.count)
                completion.selected = 0;
            editorSetEvent(L("completion.select"),
                           completion.selected + 1, completion.count);
            return 1;
        case ACTION_INSERT_NEWLINE:
            applySelectedCompletion();
            return 1;
        case ACTION_CANCEL:
            editorResetCompletion();
            editorSetEvent("%s", L("completion.canceled"));
            return 1;
        case MOUSE_HOVER: {
            int index = completionIndexAtMouse();
            if (index >= 0) completion.selected = index;
            return 1;
        }
        case MOUSE_PRESS: {
            int index = completionIndexAtMouse();
            if (index < 0) {
                editorResetCompletion();
                return 0;
            }
            completion.selected = index;
            applySelectedCompletion();
            suppress_mouse_release = 1;
            return 1;
        }
        case WINDOW_RESIZED:
            return 0;
        default:
            return 0;
    }
}
