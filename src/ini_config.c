#include "ini_config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *duplicateText(const char *text) {
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

static char *trim(char *text) {
    while (*text && isspace((unsigned char)*text)) text++;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return text;
}

static void setError(struct iniError *error, enum iniErrorKind kind,
                     int line, const char *text) {
    error->kind = kind;
    error->line = line;
    snprintf(error->text, sizeof(error->text), "%s", text ? text : "");
}

static int appendItem(struct iniDocument *document, enum iniItemType type,
                      int line, const char *section, const char *key,
                      const char *value, const char *text) {
    struct iniItem *items = realloc(
        document->items, sizeof(*items) * (size_t)(document->count + 1));
    if (!items) return 0;
    document->items = items;
    struct iniItem *item = &items[document->count];
    memset(item, 0, sizeof(*item));
    item->type = type;
    item->line = line;
    item->section = duplicateText(section ? section : "");
    item->key = key ? duplicateText(key) : NULL;
    item->value = value ? duplicateText(value) : NULL;
    item->text = duplicateText(text ? text : "");
    document->count++;
    if (!item->section || (key && !item->key) || (value && !item->value) ||
        !item->text) return 0;
    return 1;
}

int iniDocumentLoad(const char *path, size_t line_capacity,
                    struct iniDocument *document, struct iniError *error) {
    memset(document, 0, sizeof(*document));
    memset(error, 0, sizeof(*error));
    FILE *file = fopen(path, "rb");
    if (!file) {
        setError(error, INI_ERROR_OPEN, 0, path);
        return 0;
    }
    char *raw = malloc(line_capacity);
    if (!raw) {
        fclose(file);
        setError(error, INI_ERROR_ALLOCATION, 0, path);
        return 0;
    }
    char section[256] = "";
    int line = 0;
    while (fgets(raw, (int)line_capacity, file)) {
        line++;
        if (!strchr(raw, '\n') && !feof(file)) {
            setError(error, INI_ERROR_LINE_TOO_LONG, line, raw);
            goto failure;
        }
        char *text = trim(raw);
        if (!text[0] || text[0] == ';' || text[0] == '#') continue;
        if (text[0] == '[') {
            size_t length = strlen(text);
            if (length < 3 || text[length - 1] != ']') {
                setError(error, INI_ERROR_INVALID_SECTION, line, text);
                goto failure;
            }
            text[length - 1] = '\0';
            char *name = trim(text + 1);
            if (!name[0] || strlen(name) >= sizeof(section)) {
                setError(error, INI_ERROR_INVALID_SECTION, line, text);
                goto failure;
            }
            snprintf(section, sizeof(section), "%s", name);
            if (!appendItem(document, INI_ITEM_SECTION, line, section,
                            NULL, NULL, text)) {
                setError(error, INI_ERROR_ALLOCATION, line, text);
                goto failure;
            }
            continue;
        }
        char *equals = strchr(text, '=');
        if (!equals) {
            setError(error, INI_ERROR_MISSING_EQUALS, line, text);
            goto failure;
        }
        *equals = '\0';
        char *key = trim(text);
        char *value = trim(equals + 1);
        if (!appendItem(document, INI_ITEM_VALUE, line, section,
                        key, value, text)) {
            setError(error, INI_ERROR_ALLOCATION, line, text);
            goto failure;
        }
    }
    free(raw);
    fclose(file);
    return 1;

failure:
    free(raw);
    fclose(file);
    iniDocumentFree(document);
    return 0;
}

void iniDocumentFree(struct iniDocument *document) {
    for (int i = 0; i < document->count; i++) {
        free(document->items[i].section);
        free(document->items[i].key);
        free(document->items[i].value);
        free(document->items[i].text);
    }
    free(document->items);
    memset(document, 0, sizeof(*document));
}
