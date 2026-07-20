#include "common.h"
#include "syntax.h"
#include "logger.h"
#include "paths.h"
#include "row.h"
#include "colors.h"
#include "localization.h"
#include "ini_config.h"

#define MAX_LANGUAGES 32
#define MAX_KEYWORD_GROUPS 32

struct stringList {
    char **items;
    int count;
};

struct keywordGroup {
    char name[64];
    struct stringList words;
    enum ansiColor color;
    int words_seen;
    int color_seen;
};

struct languageDefinition {
    char name[64];
    struct stringList extensions;
    struct keywordGroup keyword_groups[MAX_KEYWORD_GROUPS];
    int keyword_group_count;
    char line_comment[16];
    char block_comment_start[16];
    char block_comment_end[16];
    char section_start[16];
    char section_end[16];
    struct stringList multiline_strings;
    enum ansiColor comment_color;
    enum ansiColor string_color;
    enum ansiColor number_color;
    enum ansiColor variable_color;
    enum ansiColor function_color;
    enum ansiColor section_color;
};

static struct languageDefinition languages[MAX_LANGUAGES];
static int language_count = 0;
static struct languageDefinition *current_language = NULL;

static char *trim(char *text) {
    while (*text && isspace((unsigned char)*text)) text++;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return text;
}

static _Noreturn void languageError(const char *file, int line,
                                    const char *message, const char *detail) {
    if (detail && detail[0])
        loggerFatal(EDITOR_LANGUAGES_DIR "\\%s.ini:%d: %s: %s",
                    file, line, message, detail);
    loggerFatal(EDITOR_LANGUAGES_DIR "\\%s.ini:%d: %s",
                file, line, message);
}

static void addListItem(struct stringList *list, const char *value) {
    char **items = realloc(list->items, sizeof(char *) * (list->count + 1));
    if (!items) loggerFatal("%s", L("syntax.allocation_failed"));
    list->items = items;
    list->items[list->count++] = editor_strdup(value);
}

static void parseList(const char *file, int line, const char *value,
                      struct stringList *list) {
    char *copy = editor_strdup(value);
    char *at = copy;
    for (;;) {
        char *comma = strchr(at, ',');
        if (comma) *comma = '\0';
        char *item = trim(at);
        if (!item[0]) languageError(file, line, L("syntax.empty_list_item"), value);
        addListItem(list, item);
        if (!comma) break;
        at = comma + 1;
    }
    free(copy);
}

static enum ansiColor parseColor(const char *file, int line,
                                 const char *value) {
    enum ansiColor color;
    if (!editorColorFromName(value, &color))
        languageError(file, line, L("config.unknown_color"), value);
    return color;
}

static int validLanguageName(const char *name) {
    if (!name[0]) return 0;
    for (const unsigned char *at = (const unsigned char *)name; *at; at++)
        if (!isalnum(*at) && *at != '-' && *at != '_') return 0;
    return 1;
}

static struct keywordGroup *addKeywordGroup(
    struct languageDefinition *language, const char *file, int line,
    const char *name) {
    if (!validLanguageName(name))
        languageError(file, line, L("syntax.invalid_group"), name);
    for (int i = 0; i < language->keyword_group_count; i++)
        if (_stricmp(language->keyword_groups[i].name, name) == 0)
            languageError(file, line, L("syntax.duplicate_group"), name);
    if (language->keyword_group_count >= MAX_KEYWORD_GROUPS)
        languageError(file, line, L("syntax.too_many_groups"), name);
    struct keywordGroup *group =
        &language->keyword_groups[language->keyword_group_count++];
    memset(group, 0, sizeof(*group));
    snprintf(group->name, sizeof(group->name), "%s", name);
    return group;
}

static void clearLanguages(void) {
    for (int language = 0; language < language_count; language++) {
        for (int item = 0;
             item < languages[language].extensions.count; item++)
            free(languages[language].extensions.items[item]);
        free(languages[language].extensions.items);
        for (int item = 0;
             item < languages[language].multiline_strings.count; item++)
            free(languages[language].multiline_strings.items[item]);
        free(languages[language].multiline_strings.items);
        for (int group = 0;
             group < languages[language].keyword_group_count; group++) {
            struct stringList *words =
                &languages[language].keyword_groups[group].words;
            for (int item = 0; item < words->count; item++)
                free(words->items[item]);
            free(words->items);
        }
    }
    memset(languages, 0, sizeof(languages));
    language_count = 0;
    current_language = NULL;
}

static int extensionInValue(char *value, const char *extension) {
    char *at = value;
    for (;;) {
        char *comma = strchr(at, ',');
        if (comma) *comma = '\0';
        if (_stricmp(trim(at), extension) == 0) return 1;
        if (!comma) return 0;
        at = comma + 1;
    }
}

/* Reads only the extension metadata. The full parser is invoked solely for
   the matching file, so unrelated language themes are never loaded. */
static int languageFileSupportsExtension(const char *path,
                                         const char *extension) {
    struct iniDocument document;
    struct iniError error;
    if (!iniDocumentLoad(path, 4096, &document, &error)) return 0;
    int matches = 0;
    for (int i = 0; i < document.count; i++) {
        struct iniItem *item = &document.items[i];
        if (item->type != INI_ITEM_VALUE ||
            _stricmp(item->section, "language") != 0 ||
            _stricmp(item->key, "extensions") != 0) continue;
        matches = extensionInValue(item->value, extension);
        break;
    }
    iniDocumentFree(&document);
    return matches;
}

struct languageLoadState {
    int section;
    int extensions_seen, comment_seen;
    int block_start_seen, block_end_seen, multiline_seen;
    int section_start_seen, section_end_seen;
    int color_seen[6];
    struct keywordGroup *group;
};

static void selectLanguageSection(struct languageDefinition *language,
                                  struct languageLoadState *state,
                                  const char *name, int line, char *text) {
    state->group = NULL;
    if (_stricmp(text, "[language]") == 0) { state->section = 1; return; }
    if (_stricmp(text, "[colors]") == 0) { state->section = 2; return; }

    size_t length = strlen(text);
    if (_strnicmp(text, "[keywords.", 10) != 0 || length <= 11 ||
        text[length - 1] != ']')
        languageError(name, line, L("config.unknown_section"), text);
    char group_name[64];
    size_t group_length = length - 11;
    if (group_length >= sizeof(group_name))
        languageError(name, line, L("syntax.group_name_long"), text);
    memcpy(group_name, text + 10, group_length);
    group_name[group_length] = '\0';
    state->group = addKeywordGroup(language, name, line, group_name);
    state->section = 3;
}

static void loadLanguageSetting(struct languageDefinition *language,
                                struct languageLoadState *state,
                                const char *name, int line,
                                const char *key, char *value) {
    if (_stricmp(key, "extensions") == 0) {
        if (state->extensions_seen) languageError(name, line, L("config.duplicate"), key);
        parseList(name, line, value, &language->extensions);
        state->extensions_seen = 1;
    } else if (_stricmp(key, "line_comment") == 0) {
        if (state->comment_seen) languageError(name, line, L("config.duplicate"), key);
        if (strlen(value) >= sizeof(language->line_comment))
            languageError(name, line, L("syntax.line_comment_long"), value);
        snprintf(language->line_comment, sizeof(language->line_comment),
                 "%s", value);
        state->comment_seen = 1;
    } else if (_stricmp(key, "block_comment_start") == 0) {
        if (state->block_start_seen) languageError(name, line, L("config.duplicate"), key);
        if (strlen(value) >= sizeof(language->block_comment_start))
            languageError(name, line, L("syntax.block_start_long"), value);
        snprintf(language->block_comment_start,
                 sizeof(language->block_comment_start), "%s", value);
        state->block_start_seen = 1;
    } else if (_stricmp(key, "block_comment_end") == 0) {
        if (state->block_end_seen) languageError(name, line, L("config.duplicate"), key);
        if (strlen(value) >= sizeof(language->block_comment_end))
            languageError(name, line, L("syntax.block_end_long"), value);
        snprintf(language->block_comment_end,
                 sizeof(language->block_comment_end), "%s", value);
        state->block_end_seen = 1;
    } else if (_stricmp(key, "section_start") == 0) {
        if (state->section_start_seen) languageError(name, line, L("config.duplicate"), key);
        if (strlen(value) >= sizeof(language->section_start))
            languageError(name, line, L("syntax.section_start_long"), value);
        snprintf(language->section_start, sizeof(language->section_start),
                 "%s", value);
        state->section_start_seen = 1;
    } else if (_stricmp(key, "section_end") == 0) {
        if (state->section_end_seen) languageError(name, line, L("config.duplicate"), key);
        if (strlen(value) >= sizeof(language->section_end))
            languageError(name, line, L("syntax.section_end_long"), value);
        snprintf(language->section_end, sizeof(language->section_end),
                 "%s", value);
        state->section_end_seen = 1;
    } else if (_stricmp(key, "multiline_strings") == 0) {
        if (state->multiline_seen) languageError(name, line, L("config.duplicate"), key);
        parseList(name, line, value, &language->multiline_strings);
        for (int i = 0; i < language->multiline_strings.count; i++)
            if (strlen(language->multiline_strings.items[i]) >= 16)
                languageError(name, line, L("syntax.multiline_long"),
                              language->multiline_strings.items[i]);
        state->multiline_seen = 1;
    } else languageError(name, line, L("syntax.unknown_language_setting"), key);
}

static void loadLanguageColor(struct languageDefinition *language,
                              struct languageLoadState *state,
                              const char *name, int line,
                              const char *key, const char *value) {
    static const char *names[] = {
        "comment", "string", "number", "variable", "function", "section"
    };
    enum ansiColor *targets[] = {
        &language->comment_color, &language->string_color,
        &language->number_color, &language->variable_color,
        &language->function_color, &language->section_color
    };
    int index = 0;
    while (index < 6 && _stricmp(key, names[index]) != 0) index++;
    if (index == 6) languageError(name, line, L("syntax.unknown_color_setting"), key);
    if (state->color_seen[index]) languageError(name, line, L("config.duplicate"), key);
    *targets[index] = parseColor(name, line, value);
    state->color_seen[index] = 1;
}

static void loadKeywordSetting(struct keywordGroup *group, const char *name,
                               int line, const char *key, char *value) {
    if (!group) languageError(name, line, L("syntax.invalid_keyword_section"), key);
    if (_stricmp(key, "color") == 0) {
        if (group->color_seen) languageError(name, line, L("config.duplicate"), key);
        group->color = parseColor(name, line, value);
        group->color_seen = 1;
    } else if (_stricmp(key, "words") == 0) {
        if (group->words_seen) languageError(name, line, L("config.duplicate"), key);
        parseList(name, line, value, &group->words);
        group->words_seen = 1;
    } else languageError(name, line, L("syntax.unknown_keyword_setting"), key);
}

static int keywordDefinedEarlier(const struct languageDefinition *language,
                                 int group, int word) {
    const char *candidate = language->keyword_groups[group].words.items[word];
    for (int previous_group = 0; previous_group <= group; previous_group++) {
        int limit = previous_group == group
            ? word : language->keyword_groups[previous_group].words.count;
        for (int previous_word = 0; previous_word < limit; previous_word++)
            if (strcmp(candidate, language->keyword_groups[previous_group]
                                  .words.items[previous_word]) == 0)
                return 1;
    }
    return 0;
}

static void validateKeywordGroups(const struct languageDefinition *language,
                                  const char *name) {
    for (int group = 0; group < language->keyword_group_count; group++) {
        const struct keywordGroup *current = &language->keyword_groups[group];
        if (!current->words_seen || !current->color_seen ||
            current->words.count == 0)
            loggerFatal(L("syntax.group_incomplete"),
                        name, current->name);
        for (int word = 0; word < current->words.count; word++)
            if (keywordDefinedEarlier(language, group, word))
                loggerFatal(L("syntax.duplicate_keyword"),
                            name, current->words.items[word]);
    }
}

static const char *extensionOwner(const char *extension) {
    for (int language = 0; language < language_count; language++)
        for (int item = 0; item < languages[language].extensions.count; item++)
            if (_stricmp(extension,
                         languages[language].extensions.items[item]) == 0)
                return languages[language].name;
    return NULL;
}

static void validateExtensions(const struct languageDefinition *language,
                               const char *name) {
    for (int i = 0; i < language->extensions.count; i++) {
        const char *extension = language->extensions.items[i];
        if (!validLanguageName(extension))
            loggerFatal(L("syntax.invalid_extension"), name, extension);
        const char *owner = extensionOwner(extension);
        if (owner)
            loggerFatal(L("syntax.extension_conflict"),
                        extension, owner, name);
    }
}

static void validateLanguage(const struct languageDefinition *language,
                             const struct languageLoadState *state,
                             const char *name) {
    if (!state->extensions_seen || !state->comment_seen ||
        language->keyword_group_count == 0)
        loggerFatal(L("syntax.language_incomplete"), name);
    if (state->block_start_seen != state->block_end_seen)
        loggerFatal(L("syntax.block_pair_required"),
                    name);
    if (state->section_start_seen != state->section_end_seen ||
        state->section_start_seen != state->color_seen[5])
        loggerFatal(L("syntax.section_settings_required"),
                    name);
    for (int i = 0; i < 5; i++)
        if (!state->color_seen[i])
            loggerFatal(L("syntax.colors_incomplete"), name);
    validateKeywordGroups(language, name);
    validateExtensions(language, name);
}

static void loadLanguageFile(const char *name, const char *path) {
    if (language_count >= MAX_LANGUAGES)
        loggerFatal(L("syntax.too_many_languages"), MAX_LANGUAGES);
    struct languageDefinition *language = &languages[language_count];
    memset(language, 0, sizeof(*language));
    snprintf(language->name, sizeof(language->name), "%s", name);

    struct iniDocument document;
    struct iniError error;
    if (!iniDocumentLoad(path, 4096, &document, &error)) {
        if (error.kind == INI_ERROR_OPEN)
            loggerFatal(L("syntax.open_failed"), path);
        const char *message = error.kind == INI_ERROR_MISSING_EQUALS
            ? L("config.missing_equals") : L("config.unknown_section");
        languageError(name, error.line, message, error.text);
    }

    struct languageLoadState state = {0};
    for (int item_index = 0; item_index < document.count; item_index++) {
        struct iniItem *item = &document.items[item_index];
        if (item->type == INI_ITEM_SECTION) {
            char section[272];
            snprintf(section, sizeof(section), "[%s]", item->section);
            selectLanguageSection(language, &state, name, item->line, section);
            continue;
        }
        if (!state.section)
            languageError(name, item->line, L("config.outside_section"),
                          item->text);
        if (!item->key[0] || !item->value[0])
            languageError(name, item->line, L("config.empty"), item->text);
        if (state.section == 1)
            loadLanguageSetting(language, &state, name, item->line,
                                item->key, item->value);
        else if (state.section == 2)
            loadLanguageColor(language, &state, name, item->line,
                              item->key, item->value);
        else
            loadKeywordSetting(state.group, name, item->line,
                               item->key, item->value);
        loggerLog(LOG_PASS, L("log.language_line"), name, item->line,
                  item->key, item->value);
    }
    iniDocumentFree(&document);
    validateLanguage(language, &state, name);
    language_count++;
    loggerLog(LOG_INFO, L("log.language_validated"), name);
}

static int findLanguageFile(const char *extension, char *found_name,
                            size_t name_size, char *found_path,
                            size_t path_size) {
    char pattern[MAX_PATH];
    char directory[MAX_PATH];
    WIN32_FIND_DATAA data;
    if (!editorExecutablePath(EDITOR_LANGUAGES_DIR "\\*.ini",
                              pattern, sizeof(pattern)) ||
        !editorExecutablePath(EDITOR_LANGUAGES_DIR "\\",
                              directory, sizeof(directory)))
        loggerFatal("%s", L("syntax.directory_path_long"));

    HANDLE search = FindFirstFileA(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) return 0;
    int found = 0;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        size_t length = strlen(data.cFileName);
        if (length <= 4 || _stricmp(data.cFileName + length - 4, ".ini") != 0)
            continue;
        char name[64], path[MAX_PATH];
        if (length - 4 >= sizeof(name))
            loggerFatal(L("syntax.filename_long"), data.cFileName);
        memcpy(name, data.cFileName, length - 4);
        name[length - 4] = '\0';
        if (!validLanguageName(name))
            loggerFatal(L("syntax.invalid_filename"), data.cFileName);
        if (snprintf(path, sizeof(path), "%s%s", directory, data.cFileName) >=
            (int)sizeof(path))
            loggerFatal(L("syntax.path_long"), data.cFileName);
        if (!languageFileSupportsExtension(path, extension)) continue;
        if (found)
            loggerFatal(L("syntax.multiple_matches"), extension);
        snprintf(found_name, name_size, "%s", name);
        snprintf(found_path, path_size, "%s", path);
        found = 1;
    } while (FindNextFileA(search, &data));
    FindClose(search);
    return found;
}

void editorSelectLanguageForFile(const char *filename) {
    if (!filename) {
        clearLanguages();
        return;
    }
    const char *base = strrchr(filename, '\\');
    const char *forward = strrchr(filename, '/');
    if (!base || (forward && forward > base)) base = forward;
    base = base ? base + 1 : filename;
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base || !dot[1]) {
        clearLanguages();
        return;
    }
    const char *extension = dot + 1;

    for (int i = 0; i < language_count; i++) {
        for (int j = 0; j < languages[i].extensions.count; j++) {
            if (_stricmp(extension, languages[i].extensions.items[j]) == 0) {
                current_language = &languages[i];
                editorSetColor(COLOR_SYNTAX_COMMENT,
                               current_language->comment_color);
                editorSetColor(COLOR_SYNTAX_STRING,
                               current_language->string_color);
                editorSetColor(COLOR_SYNTAX_NUMBER,
                               current_language->number_color);
                editorSetColor(COLOR_SYNTAX_VARIABLE,
                               current_language->variable_color);
                editorSetColor(COLOR_SYNTAX_FUNCTION,
                               current_language->function_color);
                loggerLog(LOG_INFO, L("log.language_selected"),
                          extension, current_language->name);
                return;
            }
        }
    }

    clearLanguages();
    char name[64], path[MAX_PATH];
    if (findLanguageFile(extension, name, sizeof(name), path, sizeof(path))) {
        loggerLog(LOG_INFO, L("log.language_loaded"),
                  extension, name);
        loadLanguageFile(name, path);
        current_language = &languages[0];
        editorSetColor(COLOR_SYNTAX_COMMENT,
                       current_language->comment_color);
        editorSetColor(COLOR_SYNTAX_STRING,
                       current_language->string_color);
        editorSetColor(COLOR_SYNTAX_NUMBER,
                       current_language->number_color);
        editorSetColor(COLOR_SYNTAX_VARIABLE,
                       current_language->variable_color);
        editorSetColor(COLOR_SYNTAX_FUNCTION,
                       current_language->function_color);
        loggerLog(LOG_INFO, L("log.language_selected"),
                  extension, current_language->name);
        return;
    }
    loggerLog(LOG_INFO, L("log.language_missing"), extension);
}

void editorRefreshAllSyntax(void) {
    for (int i = 0; i < E.numrows; i++) editorUpdateSyntax(&E.row[i]);
}

int editorSyntaxEnabled(void) {
    return current_language != NULL;
}

const char *editorSyntaxLineComment(void) {
    return current_language ? current_language->line_comment : "";
}

const char *editorSyntaxBlockCommentStart(void) {
    return current_language ? current_language->block_comment_start : "";
}

const char *editorSyntaxBlockCommentEnd(void) {
    return current_language ? current_language->block_comment_end : "";
}

const char *editorSyntaxSectionStart(void) {
    return current_language ? current_language->section_start : "";
}

const char *editorSyntaxSectionEnd(void) {
    return current_language ? current_language->section_end : "";
}

int editorSyntaxSectionColor(void) {
    return current_language
        ? (int)current_language->section_color
        : editorColorCode(COLOR_TEXT);
}

int editorSyntaxMultilineStringCount(void) {
    return current_language ? current_language->multiline_strings.count : 0;
}

const char *editorSyntaxMultilineStringAt(int index) {
    if (!current_language || index < 0 ||
        index >= current_language->multiline_strings.count)
        return "";
    return current_language->multiline_strings.items[index];
}

int editorSyntaxKeywordMatch(const char *text, int *length) {
    if (!current_language) return HL_NORMAL;
    for (int group = 0; group < current_language->keyword_group_count;
         group++) {
        struct stringList *words =
            &current_language->keyword_groups[group].words;
        for (int word = 0; word < words->count; word++) {
            int keyword_length = (int)strlen(words->items[word]);
            if (strncmp(text, words->items[word], keyword_length) == 0 &&
                is_separator(text[keyword_length])) {
                *length = keyword_length;
                return HL_LANGUAGE_BASE + group;
            }
        }
    }
    return HL_NORMAL;
}

int editorSyntaxKeywordColor(int highlight) {
    int group = highlight - HL_LANGUAGE_BASE;
    if (!current_language || group < 0 ||
        group >= current_language->keyword_group_count)
        return editorColorCode(COLOR_TEXT);
    return (int)current_language->keyword_groups[group].color;
}

static int keywordHasPrefix(const char *keyword, const char *prefix,
                            int prefix_length) {
    int keyword_length = (int)strlen(keyword);
    return keyword_length > prefix_length &&
           strncmp(keyword, prefix, prefix_length) == 0;
}

int editorSyntaxCompletionCount(const char *prefix, int prefix_length) {
    if (!current_language || !prefix || prefix_length <= 0) return 0;
    int count = 0;
    for (int group = 0; group < current_language->keyword_group_count;
         group++) {
        struct stringList *words =
            &current_language->keyword_groups[group].words;
        for (int word = 0; word < words->count; word++)
            if (keywordHasPrefix(words->items[word], prefix, prefix_length))
                count++;
    }
    return count;
}

const char *editorSyntaxCompletionAt(const char *prefix, int prefix_length,
                                     int index) {
    if (!current_language || !prefix || prefix_length <= 0 || index < 0)
        return NULL;
    for (int group = 0; group < current_language->keyword_group_count;
         group++) {
        struct stringList *words =
            &current_language->keyword_groups[group].words;
        for (int word = 0; word < words->count; word++) {
            const char *candidate = words->items[word];
            if (!keywordHasPrefix(candidate, prefix, prefix_length)) continue;
            if (index-- == 0) return candidate;
        }
    }
    return NULL;
}
