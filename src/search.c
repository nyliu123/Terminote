#include "common.h"
#include "row.h"
#include "search.h"

void editorSearchFree(struct searchResults *results) {
    free(results->items);
    results->items = NULL;
    results->count = results->capacity = 0;
}

void editorSearchCollect(struct searchResults *results, const char *query) {
    editorSearchFree(results);
    for (int row = 0; row < E.numrows; row++) {
        char *found = E.row[row].chars;
        while ((found = strstr(found, query)) != NULL) {
            if (results->count == results->capacity) {
                results->capacity = results->capacity
                    ? results->capacity * 2 : 256;
                results->items = realloc(
                    results->items,
                    results->capacity * sizeof(*results->items));
                if (!results->items) die("realloc: matches");
            }
            results->items[results->count].row = row;
            results->items[results->count].col =
                (int)(found - E.row[row].chars);
            results->count++;
            found += utf8_char_len((unsigned char)*found);
        }
    }
}

int editorSearchIndexAtOrAfter(const struct searchResults *results,
                               int row, int col) {
    for (int i = 0; i < results->count; i++) {
        struct findMatch *match = &results->items[i];
        if (match->row > row || (match->row == row && match->col >= col))
            return i;
    }
    return 0;
}

void editorSearchShowMatch(const struct findMatch *match, int query_length,
                           int reset_vertical_scroll) {
    E.wheel_scrolling = 0;
    E.cy = match->row;
    E.cx = match->col + query_length;
    if (reset_vertical_scroll) E.rowoff = E.numrows;

    int text_width = E.screencols - E.linenum_width - 1;
    if (text_width < 1) text_width = 1;
    erow *row = &E.row[match->row];
    int start = editorRowCxToRx(row, match->col);
    int width = editorRowCxToRx(row, match->col + query_length) - start;
    E.coloff = width < text_width
        ? start - (text_width - width) / 2 : start;
    if (E.coloff < 0) E.coloff = 0;
}

char *editorSearchHighlight(const struct findMatch *match, int query_length) {
    erow *row = &E.row[match->row];
    char *saved = malloc(row->rsize);
    if (!saved) return NULL;

    memcpy(saved, row->hl, row->rsize);
    int start = editorRowCxToRenderIndex(row, match->col);
    int end = editorRowCxToRenderIndex(row, match->col + query_length);
    memset(&row->hl[start], HL_MATCH, end - start);
    return saved;
}

void editorSearchRestoreHighlight(const struct findMatch *match, char *saved) {
    if (saved && match->row >= 0 && match->row < E.numrows)
        memcpy(E.row[match->row].hl, saved, E.row[match->row].rsize);
    free(saved);
}
