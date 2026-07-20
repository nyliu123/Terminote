#include "common.h"
#include "localization.h"
#include "paths.h"
#include "brand.h"
#include "ini_config.h"

#define MAX_LOCALIZED_STRINGS 512

struct localizedString {
    char *key;
    char *value;
};

static struct localizedString strings[MAX_LOCALIZED_STRINGS];
static unsigned char fallback_overridden[MAX_LOCALIZED_STRINGS];
static int string_count = 0;
static char active_language[16] = "zh";

static const char *required_status_keys[] = {
    "settings.button", "settings.event", "settings.open_failed",
    "settings.languages_missing", "editor.ready", "editor.content_drag",
    "selection.all", "selection.lines", "selection.characters",
    "status.statistics", "tabs.switched", "tabs.opened",
    "tabs.new_untitled", "tabs.closed", "file.open_path_failed",
    "find.prompt", "find.not_found", "find.current",
    "completion.completed", "completion.event", "completion.select",
    "completion.canceled", "undo.empty", "undo.event", "redo.empty",
    "redo.event", "replace.prompt", "replace.confirm", "replace.find",
    "replace.with", "replace.completed", "replace.skipped",
    "selection.cut", "selection.none", "selection.copied",
    "selection.copied_count", "selection.cut_event", "edit.indent",
    "edit.unindent", "edit.newline", "edit.cursor_move", "edit.delete",
    "edit.page", "edit.cancel", "edit.pasted_count", "edit.atomic_paste",
    "edit.pasted", "edit.character", "file.saved_as", "file.saved_name",
    "file.save_path_long", "file.save_dialog_failed", "file.save_canceled",
    "file.save_path_failed", "file.open_dialog_failed", "file.save_failed",
    "file.saved_bytes", "restart.snapshot_failed", "restart.failed",
    "run.save_first", "run.no_extension",
    "run.not_configured", "run.event", "run.started", "run.failed",
    "mouse.vertical_scroll", "mouse.horizontal_scroll", NULL
};

static _Noreturn void localizationFatal(const char *message,
                                        const char *detail) {
    fprintf(stderr, "%s: localization error: %s%s%s\n", brandLowerName(),
            message, detail && detail[0] ? ": " : "", detail ? detail : "");
    exit(1);
}

int localizationValidLanguageName(const char *name) {
    if (!name || !name[0] || strlen(name) >= sizeof(active_language)) return 0;
    for (const unsigned char *at = (const unsigned char *)name; *at; at++)
        if (!isalnum(*at) && *at != '-' && *at != '_') return 0;
    return 1;
}

static void readConfiguredLanguage(void) {
    char path[MAX_PATH];
    if (!editorExecutablePath(EDITOR_PREFERENCES_FILE, path, sizeof(path)))
        localizationFatal("preferences path is too long", NULL);
    struct iniDocument document;
    struct iniError error;
    if (!iniDocumentLoad(path, 512, &document, &error))
        localizationFatal("cannot parse config\\preferences.ini", error.text);

    int found = 0;
    for (int i = 0; i < document.count; i++) {
        struct iniItem *item = &document.items[i];
        if (item->type != INI_ITEM_VALUE ||
            _stricmp(item->section, "general") != 0 ||
            _stricmp(item->key, "language") != 0) continue;
        if (found) localizationFatal("language is defined more than once", NULL);
        if (!localizationValidLanguageName(item->value))
            localizationFatal("invalid language abbreviation", item->value);
        snprintf(active_language, sizeof(active_language), "%s", item->value);
        found = 1;
    }
    iniDocumentFree(&document);
    if (!found) localizationFatal("missing general setting", "language");
}

static void decodeValue(char *value) {
    char *read = value;
    char *write = value;
    while (*read) {
        if (*read != '\\') {
            *write++ = *read++;
            continue;
        }
        read++;
        if (*read == 'n') *write++ = '\n';
        else if (*read == 't') *write++ = '\t';
        else if (*read == 's') *write++ = ' ';
        else if (*read == '\\') *write++ = '\\';
        else localizationFatal("unknown escape sequence", read);
        if (*read) read++;
    }
    *write = '\0';
}

static void loadLanguageFile(const char *language, int fallback_count) {
    char relative[MAX_PATH];
    char path[MAX_PATH];
    if (snprintf(relative, sizeof(relative), EDITOR_LOCALIZATION_DIR "\\%s.ini",
                 language) >= (int)sizeof(relative) ||
        !editorExecutablePath(relative, path, sizeof(path)))
        localizationFatal("language pack path is too long", active_language);
    struct iniDocument document;
    struct iniError error;
    if (!iniDocumentLoad(path, 2048, &document, &error))
        localizationFatal("cannot parse language pack", relative);

    int in_section = 0;
    for (int item_index = 0; item_index < document.count; item_index++) {
        struct iniItem *item = &document.items[item_index];
        if (item->type == INI_ITEM_SECTION) {
            for (const unsigned char *at =
                     (const unsigned char *)item->section; *at; at++)
                if (!isalnum(*at) && *at != '_' && *at != '-')
                    localizationFatal("invalid language pack section",
                                      item->section);
            in_section = 1;
            continue;
        }
        if (!in_section)
            localizationFatal("string is outside a module section", item->text);
        char *key = item->key;
        char *value = item->value;
        if (!key[0] || !value[0])
            localizationFatal("empty language pack key or value", item->text);
        int existing = -1;
        for (int i = 0; i < string_count; i++)
            if (_stricmp(strings[i].key, key) == 0) { existing = i; break; }
        if (existing >= fallback_count ||
            (existing >= 0 && fallback_overridden[existing]))
            localizationFatal("duplicate language pack key", key);
        if (string_count >= MAX_LOCALIZED_STRINGS)
            localizationFatal("too many language pack strings", NULL);
        decodeValue(value);
        if (existing >= 0) {
            free(strings[existing].value);
            strings[existing].value = brandExpandText(value);
            if (!strings[existing].value)
                localizationFatal("cannot allocate localized string", key);
            fallback_overridden[existing] = 1;
            continue;
        }
        strings[string_count].key = editor_strdup(key);
        strings[string_count].value = brandExpandText(value);
        if (!strings[string_count].key || !strings[string_count].value)
            localizationFatal("cannot allocate localized string", key);
        string_count++;
    }
    iniDocumentFree(&document);
    if (!in_section || string_count == 0)
        localizationFatal("language pack contains no strings", relative);
}

static void validateCurrentLanguageStatusStrings(void) {
    if (_stricmp(active_language, "en") == 0) return;
    for (int key = 0; required_status_keys[key]; key++) {
        int found = 0;
        for (int i = 0; i < string_count; i++) {
            if (strcmp(strings[i].key, required_status_keys[key]) != 0)
                continue;
            found = fallback_overridden[i];
            break;
        }
        if (!found)
            localizationFatal("language pack is missing a status string",
                              required_status_keys[key]);
    }
}

static void validateCurrentLanguageCoverage(int fallback_count) {
    if (_stricmp(active_language, "en") == 0) return;
    for (int i = 0; i < fallback_count; i++)
        if (!fallback_overridden[i])
            localizationFatal("language pack is missing a localized string",
                              strings[i].key);
}

void localizationInitialize(void) {
    readConfiguredLanguage();
    if (_stricmp(active_language, "en") != 0) {
        loadLanguageFile("en", 0);
        int fallback_count = string_count;
        loadLanguageFile(active_language, fallback_count);
        validateCurrentLanguageCoverage(fallback_count);
    } else {
        loadLanguageFile("en", 0);
    }
    validateCurrentLanguageStatusStrings();
}

const char *localizationGet(const char *key) {
    for (int i = 0; i < string_count; i++)
        if (strcmp(strings[i].key, key) == 0) return strings[i].value;
    return key;
}

const char *localizationLanguage(void) {
    return active_language;
}
