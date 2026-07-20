#ifndef SEARCH_H
#define SEARCH_H

struct searchResults {
    struct findMatch *items;
    int count;
    int capacity;
};

void editorSearchCollect(struct searchResults *results, const char *query);
void editorSearchFree(struct searchResults *results);
int editorSearchIndexAtOrAfter(const struct searchResults *results,
                               int row, int col);
void editorSearchShowMatch(const struct findMatch *match, int query_length,
                           int reset_vertical_scroll);
char *editorSearchHighlight(const struct findMatch *match, int query_length);
void editorSearchRestoreHighlight(const struct findMatch *match, char *saved);

#endif
