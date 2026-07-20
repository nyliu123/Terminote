#include "common.h"
#include "preferences.h"
#include "logger.h"
#include "paths.h"
#include "colors.h"
#include "localization.h"
#include "ini_config.h"
#include <stddef.h>

struct themeSetting {
    const char *name;
    enum editorColorSlot slot;
};

struct generalSetting {
    const char *name;
    size_t offset;
    int boolean;
    int maximum;
};

#define CONFIG_OFFSET(member) offsetof(struct editorConfig, member)
static const struct generalSetting general_settings[] = {
    {"ready_timeout_ms", CONFIG_OFFSET(ready_timeout_ms), 0, INT_MAX},
    {"auto_indent", CONFIG_OFFSET(auto_indent), 1, 1},
    {"auto_pair", CONFIG_OFFSET(auto_pair), 1, 1},
    {"code_completion", CONFIG_OFFSET(code_completion), 1, 1},
    {"bracket_matching", CONFIG_OFFSET(bracket_matching), 1, 1},
    {"tab_width", CONFIG_OFFSET(tab_width), 0, 32},
    {NULL, 0, 0, 0}
};

static const struct themeSetting theme_settings[] = {
    {"normal_text", COLOR_TEXT},
    {"line_number", COLOR_LINE_NUMBER},
    {"active_line_number", COLOR_ACTIVE_LINE_NUMBER},
    {"active_line_background", COLOR_ACTIVE_LINE_BACKGROUND},
    {"tab_bar", COLOR_TAB_BAR},
    {"tab_dropdown_background", COLOR_TAB_DROPDOWN_BACKGROUND},
    {"tab_dropdown_text", COLOR_TAB_DROPDOWN_TEXT},
    {"tab_dropdown_hover_background", COLOR_TAB_DROPDOWN_HOVER_BACKGROUND},
    {"tab_dropdown_hover_text", COLOR_TAB_DROPDOWN_HOVER_TEXT},
    {"status_bar", COLOR_STATUS_BAR},
    {"settings_button_hover_background", COLOR_SETTINGS_BUTTON_HOVER_BACKGROUND},
    {"settings_button_hover_text", COLOR_SETTINGS_BUTTON_HOVER_TEXT},
    {"event_bar", COLOR_EVENT_BAR},
    {"welcome_logo", COLOR_WELCOME_LOGO},
    {"welcome_description", COLOR_WELCOME_DESCRIPTION},
    {"welcome_shortcut", COLOR_WELCOME_SHORTCUT},
    {"settings_menu_background", COLOR_SETTINGS_MENU_BACKGROUND},
    {"settings_menu_text", COLOR_SETTINGS_MENU_TEXT},
    {"settings_menu_selected_background", COLOR_SETTINGS_MENU_SELECTED_BACKGROUND},
    {"settings_menu_selected_text", COLOR_SETTINGS_MENU_SELECTED_TEXT},
    {"settings_menu_hover_background", COLOR_SETTINGS_MENU_HOVER_BACKGROUND},
    {"settings_menu_hover_text", COLOR_SETTINGS_MENU_HOVER_TEXT},
    {"completion_background", COLOR_COMPLETION_BACKGROUND},
    {"completion_text", COLOR_COMPLETION_TEXT},
    {"completion_selected_background", COLOR_COMPLETION_SELECTED_BACKGROUND},
    {"completion_selected_text", COLOR_COMPLETION_SELECTED_TEXT},
    {"help_border", COLOR_HELP_BORDER},
    {"help_title", COLOR_HELP_TITLE},
    {"help_section", COLOR_HELP_SECTION},
    {"help_key", COLOR_HELP_KEY},
    {"help_description", COLOR_HELP_DESCRIPTION},
    {"help_button", COLOR_HELP_BUTTON},
    {"help_button_hover_background", COLOR_HELP_BUTTON_HOVER_BACKGROUND},
    {"help_button_hover_text", COLOR_HELP_BUTTON_HOVER_TEXT},
    {"scrollbar_track", COLOR_SCROLLBAR_TRACK},
    {"scrollbar_thumb", COLOR_SCROLLBAR_THUMB},
    {"selection", COLOR_SELECTION},
    {"selection_text", COLOR_SELECTION_TEXT},
    {"match", COLOR_MATCH},
    {"bracket_match_text", COLOR_BRACKET_MATCH_TEXT},
    {"bracket_match_background", COLOR_BRACKET_MATCH_BACKGROUND},
    {NULL, COLOR_RESET}
};

static _Noreturn void preferenceError(int line, const char *message,
                                      const char *detail) {
    if (detail && detail[0])
        loggerFatal("config\\preferences.ini:%d: %s: %s",
                    line, message, detail);
    loggerFatal("config\\preferences.ini:%d: %s", line, message);
}

static int parsePositiveInteger(const char *text, int line) {
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!text[0] || *end != '\0' || value <= 0 || value > INT_MAX)
        preferenceError(line, L("config.positive_integer"), text);
    return (int)value;
}

static int parseBoolean(const char *text, int line) {
    if (_stricmp(text, "true") == 0) return 1;
    if (_stricmp(text, "false") == 0) return 0;
    preferenceError(line, L("config.boolean"), text);
}

static enum ansiColor parseColor(const char *text, int line) {
    enum ansiColor color;
    if (!editorColorFromName(text, &color))
        preferenceError(line, L("config.unknown_color"), text);
    return color;
}

static int themeIndex(const char *name) {
    for (int i = 0; theme_settings[i].name; i++)
        if (_stricmp(name, theme_settings[i].name) == 0) return i;
    return -1;
}

static int generalIndex(const char *name) {
    for (int i = 0; general_settings[i].name; i++)
        if (_stricmp(name, general_settings[i].name) == 0) return i;
    return -1;
}

static void loadGeneralSetting(const char *name, const char *value, int line,
                               unsigned char *seen) {
    int index = generalIndex(name);
    if (index < 0) preferenceError(line, L("config.unknown_general"), name);
    if (seen[index]) preferenceError(line, L("config.duplicate"), name);

    const struct generalSetting *setting = &general_settings[index];
    int parsed = setting->boolean
        ? parseBoolean(value, line) : parsePositiveInteger(value, line);
    if (parsed > setting->maximum)
        preferenceError(line, L("config.max_32"), value);
    *(int *)((char *)&E + setting->offset) = parsed;
    seen[index] = 1;
}

static void loadThemeSetting(const char *name, const char *value, int line,
                             unsigned char *seen) {
    int index = themeIndex(name);
    if (index < 0) preferenceError(line, L("config.unknown_theme"), name);
    enum editorColorSlot slot = theme_settings[index].slot;
    if (seen[slot]) preferenceError(line, L("config.duplicate"), name);
    editorSetColor(slot, parseColor(value, line));
    seen[slot] = 1;
}

void editorLoadPreferences(void) {
    char path[MAX_PATH];
    int section = 0;
    unsigned char general_seen[7] = {0};
    unsigned char theme_seen[COLOR_COUNT] = {0};

    editorInitializeColors();
    loggerLog(LOG_INFO, "%s", L("log.preferences_start"));
    if (!editorExecutablePath(EDITOR_PREFERENCES_FILE, path, sizeof(path)))
        loggerFatal("%s", L("preferences.path_failed"));
    loggerLog(LOG_INFO, L("log.config_file"), path);

    struct iniDocument document;
    struct iniError error;
    if (!iniDocumentLoad(path, 512, &document, &error)) {
        if (error.kind == INI_ERROR_OPEN)
            loggerFatal("%s", L("preferences.open_failed"));
        if (error.kind == INI_ERROR_MISSING_EQUALS)
            preferenceError(error.line, L("config.missing_equals"), error.text);
        preferenceError(error.line, L("config.unknown_section"), error.text);
    }

    for (int item_index = 0; item_index < document.count; item_index++) {
        struct iniItem *item = &document.items[item_index];
        int line = item->line;
        if (item->type == INI_ITEM_SECTION) {
            if (_stricmp(item->section, "general") == 0) section = 1;
            else if (_stricmp(item->section, "theme") == 0) section = 2;
            else preferenceError(line, L("config.unknown_section"), item->section);
            continue;
        }
        if (section == 0)
            preferenceError(line, L("config.outside_section"), item->text);

        char *name = item->key;
        char *value = item->value;
        if (!name[0] || !value[0])
            preferenceError(line, L("config.empty"), item->text);

        if (section == 1 && _stricmp(name, "language") == 0) {
            if (general_seen[6])
                preferenceError(line, L("config.duplicate"), name);
            if (!localizationValidLanguageName(value))
                preferenceError(line, L("config.invalid_language"), value);
            if (_stricmp(value, localizationLanguage()) != 0)
                preferenceError(line, L("config.language_mismatch"), value);
            general_seen[6] = 1;
        } else if (section == 1) {
            loadGeneralSetting(name, value, line, general_seen);
        } else
            loadThemeSetting(name, value, line, theme_seen);
        loggerLog(LOG_PASS, L("log.preference_line"),
                  line, name, value);
    }
    iniDocumentFree(&document);

    for (int i = 0; general_settings[i].name; i++)
        if (!general_seen[i])
            loggerFatal("%s", L("preferences.general_incomplete"));
    if (!general_seen[6])
        loggerFatal("%s", L("preferences.language_missing"));
    for (int i = 0; theme_settings[i].name; i++) {
        enum editorColorSlot slot = theme_settings[i].slot;
        if (!theme_seen[slot])
            loggerFatal(L("preferences.theme_missing"),
                        theme_settings[i].name);
    }
    loggerLog(LOG_INFO, L("log.preferences_validated"),
              E.ready_timeout_ms, E.auto_indent, E.auto_pair,
              E.code_completion, E.bracket_matching, E.tab_width,
              localizationLanguage());
}
