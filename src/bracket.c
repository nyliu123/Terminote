#include "common.h"
#include "bracket.h"
#include "row.h"

struct bracketPosition {
    int row;
    int col;
    int render;
};

static struct bracketPosition matched[2];
static int match_active = 0;
static int match_suppressed = 0;
static int suppressed_row = 0;
static int suppressed_col = 0;
static int suppressed_dirty = 0;
static erow *suppressed_rows = NULL;

static int bracketPair(char value, char *open, char *close,
                       int *direction) {
    switch (value) {
        case '(': *open = '('; *close = ')'; *direction = 1; return 1;
        case '[': *open = '['; *close = ']'; *direction = 1; return 1;
        case '{': *open = '{'; *close = '}'; *direction = 1; return 1;
        case ')': *open = '('; *close = ')'; *direction = -1; return 1;
        case ']': *open = '['; *close = ']'; *direction = -1; return 1;
        case '}': *open = '{'; *close = '}'; *direction = -1; return 1;
        default: return 0;
    }
}

static int bracketIsCode(int row_index, int column) {
    erow *row = &E.row[row_index];
    int render = editorRowCxToRenderIndex(row, column);
    if (render < 0 || render >= row->rsize) return 0;
    return row->hl[render] != HL_COMMENT && row->hl[render] != HL_STRING;
}

static int adjacentBracket(struct bracketPosition *position,
                           char *open, char *close, int *direction) {
    if (E.cy < 0 || E.cy >= E.numrows) return 0;
    erow *row = &E.row[E.cy];
    int candidates[2] = {E.cx, editorPrevCharByte(row, E.cx)};
    for (int index = 0; index < 2; index++) {
        int column = candidates[index];
        if (column < 0 || column >= row->size) continue;
        if (!bracketPair(row->chars[column], open, close, direction)) continue;
        if (!bracketIsCode(E.cy, column)) continue;
        position->row = E.cy;
        position->col = column;
        position->render = editorRowCxToRenderIndex(row, column);
        return 1;
    }
    return 0;
}

static int setMatch(int row, int col) {
    matched[1].row = row;
    matched[1].col = col;
    matched[1].render = editorRowCxToRenderIndex(&E.row[row], col);
    match_active = 1;
    return 1;
}

static int findClosingBracket(char open, char close) {
    int depth = 0;
    for (int row_index = matched[0].row; row_index < E.numrows; row_index++) {
        erow *row = &E.row[row_index];
        int column = row_index == matched[0].row
            ? editorNextCharByte(row, matched[0].col) : 0;
        while (column < row->size) {
            char value = row->chars[column];
            if (bracketIsCode(row_index, column)) {
                if (value == open) depth++;
                else if (value == close && depth-- == 0)
                    return setMatch(row_index, column);
            }
            column = editorNextCharByte(row, column);
        }
    }
    return 0;
}

static int findOpeningBracket(char open, char close) {
    int depth = 0;
    for (int row_index = matched[0].row; row_index >= 0; row_index--) {
        erow *row = &E.row[row_index];
        int column = row_index == matched[0].row
            ? matched[0].col : row->size;
        while (column > 0) {
            column = editorPrevCharByte(row, column);
            if (!bracketIsCode(row_index, column)) continue;
            char value = row->chars[column];
            if (value == close) depth++;
            else if (value == open && depth-- == 0)
                return setMatch(row_index, column);
        }
    }
    return 0;
}

void editorUpdateBracketMatch(void) {
    match_active = 0;
    if (match_suppressed) {
        if (suppressed_rows == E.row && suppressed_row == E.cy &&
            suppressed_col == E.cx && suppressed_dirty == E.dirty)
            return;
        match_suppressed = 0;
    }
    if (!E.bracket_matching || E.numrows <= 0) return;

    char open, close;
    int direction;
    if (!adjacentBracket(&matched[0], &open, &close, &direction)) return;

    if (direction > 0) findClosingBracket(open, close);
    else findOpeningBracket(open, close);
}

void editorSuppressBracketMatchAtCursor(void) {
    match_active = 0;
    match_suppressed = 1;
    suppressed_rows = E.row;
    suppressed_row = E.cy;
    suppressed_col = E.cx;
    suppressed_dirty = E.dirty;
}

int editorBracketMatchedAt(int row, int render_index) {
    if (!match_active) return 0;
    return (matched[0].row == row && matched[0].render == render_index) ||
           (matched[1].row == row && matched[1].render == render_index);
}
