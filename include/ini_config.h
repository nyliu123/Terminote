#ifndef TERMINOTE_INI_CONFIG_H
#define TERMINOTE_INI_CONFIG_H

#include <stddef.h>

enum iniItemType {
    INI_ITEM_SECTION,
    INI_ITEM_VALUE
};

enum iniErrorKind {
    INI_ERROR_NONE,
    INI_ERROR_OPEN,
    INI_ERROR_LINE_TOO_LONG,
    INI_ERROR_INVALID_SECTION,
    INI_ERROR_MISSING_EQUALS,
    INI_ERROR_ALLOCATION
};

struct iniItem {
    enum iniItemType type;
    int line;
    char *section;
    char *key;
    char *value;
    char *text;
};

struct iniDocument {
    struct iniItem *items;
    int count;
};

struct iniError {
    enum iniErrorKind kind;
    int line;
    char text[256];
};

int iniDocumentLoad(const char *path, size_t line_capacity,
                    struct iniDocument *document, struct iniError *error);
void iniDocumentFree(struct iniDocument *document);

#endif
