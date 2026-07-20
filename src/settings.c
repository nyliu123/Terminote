#include "common.h"
#include "settings.h"
#include "output.h"
#include "colors.h"
#include "keybindings.h"
#include "paths.h"
#include "tabs.h"
#include "completion.h"
#include "localization.h"

#define SETTINGS_ITEM_COUNT 4
#define SETTINGS_LANGUAGE_ITEM 3

struct languageSetting {
    char name[64];
    char path[MAX_PATH];
};

static const char *setting_labels[SETTINGS_ITEM_COUNT] = {
    "settings.general_theme",
    "settings.keybindings",
    "settings.execution",
    "settings.languages"
};

static const char *setting_paths[SETTINGS_ITEM_COUNT - 1] = {
    EDITOR_PREFERENCES_FILE,
    EDITOR_KEY_BINDINGS_FILE,
    EDITOR_EXECUTION_FILE
};

static int popup_open = 0;
static int language_open = 0;
static int selected_item = 0;
static int selected_language = 0;
static int language_first = 0;
static int hovered_item = -1;
static int hovered_language = -1;
static int suppress_release = 0;
static int button_hovered = 0;
static struct languageSetting *languages = NULL;
static int language_count = 0;

static int textWidth(const char *text) {
    return utf8_terminal_width(text);
}

const char *editorSettingsButtonLabel(void) {
    return L("settings.button");
}

int editorSettingsButtonWidth(void) {
    return textWidth(editorSettingsButtonLabel());
}

static int compareLanguages(const void *left, const void *right) {
    const struct languageSetting *a = left;
    const struct languageSetting *b = right;
    return _stricmp(a->name, b->name);
}

static int baseLeft(void) {
    return 0;
}

static void loadLanguages(void) {
    free(languages);
    languages = NULL;
    language_count = 0;

    char pattern[MAX_PATH];
    if (!editorExecutablePath(EDITOR_LANGUAGES_DIR "\\*.ini",
                              pattern, sizeof(pattern)))
        return;
    WIN32_FIND_DATAA data;
    HANDLE search = FindFirstFileA(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        size_t length = strlen(data.cFileName);
        if (length <= 4 || _stricmp(data.cFileName + length - 4, ".ini") != 0)
            continue;
        struct languageSetting *grown = realloc(
            languages, sizeof(*languages) * (language_count + 1));
        if (!grown) die("realloc: settings languages");
        languages = grown;
        struct languageSetting *language = &languages[language_count++];
        size_t name_length = length - 4;
        if (name_length >= sizeof(language->name))
            name_length = sizeof(language->name) - 1;
        memcpy(language->name, data.cFileName, name_length);
        language->name[name_length] = '\0';
        char relative[MAX_PATH * 2];
        int relative_length = snprintf(relative, sizeof(relative),
                                       EDITOR_LANGUAGES_DIR "\\%s",
                                       data.cFileName);
        if (relative_length < 0 || relative_length >= (int)sizeof(relative) ||
            !editorExecutablePath(relative, language->path,
                                  sizeof(language->path)))
            language->path[0] = '\0';
    } while (FindNextFileA(search, &data));
    FindClose(search);
    qsort(languages, language_count, sizeof(*languages), compareLanguages);
    if (selected_language >= language_count) selected_language = 0;
}

static int baseWidth(void) {
    int available = E.screencols - baseLeft();
    if (available < 1) return 0;
    int width = 0;
    for (int i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        int candidate = textWidth(L(setting_labels[i])) + 2;
        if (candidate > width) width = candidate;
    }
    if (width > available) width = available;
    return width;
}

static int baseTop(void) {
    /* The final menu item covers the event/status row (Settings), while the
       bottom file-statistics row and its settings button remain visible. */
    int top = E.screenrows + 2 - SETTINGS_ITEM_COUNT;
    return top < 1 ? 1 : top;
}

static int languageVisible(void) {
    return language_count < E.screenrows ? language_count : E.screenrows;
}

static int languageWidth(void) {
    int width = 0;
    for (int i = 0; i < language_count; i++) {
        int candidate = textWidth(languages[i].name) + 2;
        if (candidate > width) width = candidate;
    }
    int available = E.screencols - baseLeft() - baseWidth();
    if (width > available) width = available;
    return width;
}

static int languageTop(void) {
    int top = baseTop() + SETTINGS_LANGUAGE_ITEM - languageVisible() + 1;
    return top < 1 ? 1 : top;
}

static void keepLanguageVisible(void) {
    int visible = languageVisible();
    if (visible < 1) { language_first = 0; return; }
    if (selected_language < language_first)
        language_first = selected_language;
    if (selected_language >= language_first + visible)
        language_first = selected_language - visible + 1;
    int maximum = language_count - visible;
    if (language_first > maximum) language_first = maximum;
    if (language_first < 0) language_first = 0;
}

static void appendMenuText(const char *text, int width) {
    int used = 0;
    while (*text && used < width) {
        int length;
        int columns = utf8_terminal_char_width(text, &length);
        if (used + columns > width) break;
        char character[5];
        memcpy(character, text, length);
        character[length] = '\0';
        abAppend(character);
        text += length;
        used += columns;
    }
    while (used++ < width) abAppend(" ");
}

static void appendMenuColors(int selected, int hovered) {
    if (selected) {
        abAppend(editorColor(COLOR_SETTINGS_MENU_SELECTED_BACKGROUND));
        abAppend(editorColor(COLOR_SETTINGS_MENU_SELECTED_TEXT));
    } else if (hovered) {
        abAppend(editorColor(COLOR_SETTINGS_MENU_HOVER_BACKGROUND));
        abAppend(editorColor(COLOR_SETTINGS_MENU_HOVER_TEXT));
    } else {
        abAppend(editorColor(COLOR_SETTINGS_MENU_BACKGROUND));
        abAppend(editorColor(COLOR_SETTINGS_MENU_TEXT));
    }
}

void editorDrawSettingsPopup(void) {
    if (!popup_open) return;
    int left = baseLeft();
    int width = baseWidth();
    if (width < 1) return;
    int top = baseTop();
    for (int i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        char position[32];
        snprintf(position, sizeof(position), "\x1b[%d;%dH",
                 top + i + 1, left + 1);
        abAppend(position);
        appendMenuColors(!language_open && i == selected_item,
                         i == hovered_item);
        char label[160];
        snprintf(label, sizeof(label), " %s", L(setting_labels[i]));
        appendMenuText(label, width);
        abAppend(editorColor(COLOR_RESET));
    }

    if (!language_open || language_count <= 0) return;
    keepLanguageVisible();
    int visible = languageVisible();
    int language_width = languageWidth();
    if (language_width < 1) return;
    int language_top = languageTop();
    for (int row = 0; row < visible; row++) {
        int index = language_first + row;
        if (index >= language_count) break;
        char position[32];
        snprintf(position, sizeof(position), "\x1b[%d;%dH",
                 language_top + row + 1, left + width + 1);
        abAppend(position);
        appendMenuColors(index == selected_language,
                         index == hovered_language);
        char label[96];
        snprintf(label, sizeof(label), " %s", languages[index].name);
        appendMenuText(label, language_width);
        abAppend(editorColor(COLOR_RESET));
    }
}

static void closePopup(void) {
    popup_open = 0;
    language_open = 0;
    hovered_item = -1;
    hovered_language = -1;
    button_hovered = 0;
}

static void openPopup(void) {
    editorCloseTabDropdown();
    editorResetCompletion();
    loadLanguages();
    popup_open = 1;
    language_open = 0;
    selected_item = 0;
    selected_language = 0;
    language_first = 0;
    hovered_item = -1;
    hovered_language = -1;
    editorSetEvent("%s", L("settings.event"));
}

static void openSettingsFile(const char *path) {
    closePopup();
    if (!path || !path[0] || !editorTabsOpenPath(path))
        editorSetStatusMessage("%s", L("settings.open_failed"));
}

static void activateSelection(void) {
    if (language_open) {
        if (selected_language >= 0 && selected_language < language_count)
            openSettingsFile(languages[selected_language].path);
        return;
    }
    if (selected_item == SETTINGS_LANGUAGE_ITEM) {
        if (language_count <= 0) {
            editorSetStatusMessage("%s", L("settings.languages_missing"));
            return;
        }
        language_open = 1;
        selected_language = 0;
        language_first = 0;
        return;
    }
    char path[MAX_PATH];
    if (!editorExecutablePath(setting_paths[selected_item], path, sizeof(path)))
        path[0] = '\0';
    openSettingsFile(path);
}

static int baseItemAtMouse(void) {
    int row = E.mouse_y - baseTop();
    int left = baseLeft();
    return E.mouse_x >= left && E.mouse_x < left + baseWidth() &&
           row >= 0 && row < SETTINGS_ITEM_COUNT ? row : -1;
}

static int languageAtMouse(void) {
    if (!language_open) return -1;
    int left = baseLeft();
    int width = baseWidth();
    int language_width = languageWidth();
    int row = E.mouse_y - languageTop();
    int index = language_first + row;
    return E.mouse_x >= left + width &&
           E.mouse_x < left + width + language_width &&
           row >= 0 && row < languageVisible() && index < language_count
        ? index : -1;
}

int editorHandleSettingsInput(int key) {
    int button_row = E.screenrows + 2;
    int over_button = E.mouse_y == button_row && E.mouse_x >= 0 &&
                      E.mouse_x < editorSettingsButtonWidth();

    if (!popup_open) {
        if (key == MOUSE_HOVER) {
            int was_hovered = button_hovered;
            button_hovered = over_button;
            return over_button || was_hovered;
        }
        if (key == ACTION_SETTINGS || (key == MOUSE_PRESS && over_button)) {
            openPopup();
            button_hovered = over_button;
            suppress_release = key == MOUSE_PRESS;
            return 1;
        }
        return 0;
    }

    if (suppress_release && key == MOUSE_RELEASE) {
        suppress_release = 0;
        return 1;
    }
    if (key == ACTION_SETTINGS || key == ACTION_CANCEL) {
        closePopup();
        editorSetEvent("%s", L("editor.ready"));
        return 1;
    }
    if (key == ACTION_MOVE_UP || key == ACTION_MOVE_DOWN) {
        int delta = key == ACTION_MOVE_UP ? -1 : 1;
        if (language_open && language_count > 0) {
            selected_language = (selected_language + delta + language_count) %
                                language_count;
            keepLanguageVisible();
        } else {
            selected_item = (selected_item + delta + SETTINGS_ITEM_COUNT) %
                            SETTINGS_ITEM_COUNT;
        }
        return 1;
    }
    if (key == ACTION_MOVE_RIGHT && !language_open &&
        selected_item == SETTINGS_LANGUAGE_ITEM) {
        activateSelection();
        return 1;
    }
    if (key == ACTION_MOVE_LEFT && language_open) {
        language_open = 0;
        return 1;
    }
    if (key == ACTION_INSERT_NEWLINE) {
        activateSelection();
        return 1;
    }
    if (key == MOUSE_HOVER) {
        button_hovered = over_button;
        hovered_item = baseItemAtMouse();
        hovered_language = languageAtMouse();
        return 1;
    }
    if (key == MOUSE_PRESS) {
        int language = languageAtMouse();
        int item = baseItemAtMouse();
        if (language >= 0) {
            selected_language = language;
            activateSelection();
        } else if (item >= 0) {
            selected_item = item;
            language_open = 0;
            activateSelection();
        } else if (over_button) {
            closePopup();
            editorSetEvent("%s", L("editor.ready"));
        } else {
            closePopup();
            editorSetEvent("%s", L("editor.ready"));
        }
        suppress_release = 1;
        return 1;
    }
    if (key == WHEEL_UP || key == WHEEL_DOWN || key == MOUSE_DRAG ||
        key == MOUSE_RELEASE || key == MOUSE_CLICK)
        return 1;
    return 1;
}

int editorSettingsOpen(void) { return popup_open; }
int editorSettingsButtonHovered(void) { return button_hovered; }
