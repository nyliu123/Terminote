#include "common.h"
#include "keybindings.h"
#include "logger.h"
#include "paths.h"
#include "localization.h"
#include "ini_config.h"

#define KEY_MOD_CTRL  0x01
#define KEY_MOD_SHIFT 0x02
#define KEY_MOD_ALT   0x04
#define MAX_BINDINGS 64

static char *trim(char *text) {
    while (*text && isspace((unsigned char)*text)) text++;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return text;
}

struct keyBinding {
    int action;
    WORD virtual_key;
    unsigned char modifiers;
    char label[64];
};

struct actionName {
    const char *name;
    int action;
};

static const struct actionName action_names[] = {
    {"save", ACTION_SAVE},
    {"save_as", ACTION_SAVE_AS},
    {"open", ACTION_OPEN},
    {"new_file", ACTION_NEW_FILE},
    {"next_tab", ACTION_NEXT_TAB},
    {"run", ACTION_RUN},
    {"settings", ACTION_SETTINGS},
    {"quit_without_saving", ACTION_QUIT_WITHOUT_SAVING},
    {"cut", ACTION_CUT},
    {"copy", ACTION_COPY},
    {"paste", ACTION_PASTE},
    {"select_all", ACTION_SELECT_ALL},
    {"find", ACTION_FIND},
    {"find_and_replace", ACTION_FIND_AND_REPLACE},
    {"undo", ACTION_UNDO},
    {"redo", ACTION_REDO},
    {"help", ACTION_HELP},
    {"cancel", ACTION_CANCEL},
    {"placeholder", ACTION_PLACEHOLDER},
    {"insert_newline", ACTION_INSERT_NEWLINE},
    {"indent", ACTION_INDENT},
    {"unindent", ACTION_UNINDENT},
    {"delete_backward", ACTION_DELETE_BACKWARD},
    {"delete_forward", ACTION_DELETE_FORWARD},
    {"move_up", ACTION_MOVE_UP},
    {"move_down", ACTION_MOVE_DOWN},
    {"move_left", ACTION_MOVE_LEFT},
    {"move_right", ACTION_MOVE_RIGHT},
    {"move_to_line_start", ACTION_MOVE_TO_LINE_START},
    {"move_to_line_end", ACTION_MOVE_TO_LINE_END},
    {"page_up", ACTION_PAGE_UP},
    {"page_down", ACTION_PAGE_DOWN},
    {"replace_yes", ACTION_REPLACE_YES},
    {"replace_no", ACTION_REPLACE_NO},
    {"replace_all", ACTION_REPLACE_ALL},
    {NULL, 0}
};

static struct keyBinding bindings[MAX_BINDINGS];
static int binding_count = 0;

static _Noreturn void bindingError(int line, const char *message,
                                   const char *detail) {
    char error[512];
    if (detail && detail[0])
        snprintf(error, sizeof(error),
                 "config\\key-bindings.ini:%d: %s: %s",
                 line, message, detail);
    else
        snprintf(error, sizeof(error),
                 "config\\key-bindings.ini:%d: %s", line, message);
    loggerFatal("%s", error);
}

static int actionFromName(const char *name) {
    for (int i = 0; action_names[i].name; i++)
        if (_stricmp(name, action_names[i].name) == 0)
            return action_names[i].action;
    return 0;
}

static WORD virtualKeyFromName(const char *name) {
    if (name[0] && !name[1]) {
        unsigned char key = (unsigned char)name[0];
        if (isalpha(key)) return (WORD)toupper(key);
        if (isdigit(key)) return (WORD)key;
    }

    if (_stricmp(name, "Escape") == 0) return VK_ESCAPE;
    if (_stricmp(name, "Enter") == 0) return VK_RETURN;
    if (_stricmp(name, "Tab") == 0) return VK_TAB;
    if (_stricmp(name, "Backspace") == 0) return VK_BACK;
    if (_stricmp(name, "Delete") == 0) return VK_DELETE;
    if (_stricmp(name, "Up") == 0) return VK_UP;
    if (_stricmp(name, "Down") == 0) return VK_DOWN;
    if (_stricmp(name, "Left") == 0) return VK_LEFT;
    if (_stricmp(name, "Right") == 0) return VK_RIGHT;
    if (_stricmp(name, "Home") == 0) return VK_HOME;
    if (_stricmp(name, "End") == 0) return VK_END;
    if (_stricmp(name, "PageUp") == 0) return VK_PRIOR;
    if (_stricmp(name, "PageDown") == 0) return VK_NEXT;
    if (_stricmp(name, "Space") == 0) return VK_SPACE;

    if ((name[0] == 'F' || name[0] == 'f') && isdigit((unsigned char)name[1])) {
        char *end = NULL;
        long number = strtol(name + 1, &end, 10);
        if (*end == '\0' && number >= 1 && number <= 24)
            return (WORD)(VK_F1 + number - 1);
    }
    return 0;
}

static void parseBinding(const char *value, WORD *virtual_key,
                         unsigned char *modifiers, int line) {
    char copy[128];
    if (strlen(value) >= sizeof(copy))
        bindingError(line, L("keybindings.too_long"), value);
    memcpy(copy, value, strlen(value) + 1);

    char *tokens[4];
    int token_count = 0;
    char *start = copy;
    for (;;) {
        char *plus = strchr(start, '+');
        if (plus) *plus = '\0';
        if (token_count >= 4)
            bindingError(line, L("keybindings.invalid_format"), value);
        tokens[token_count++] = trim(start);
        if (!tokens[token_count - 1][0])
            bindingError(line, L("keybindings.empty_part"), value);
        if (!plus) break;
        start = plus + 1;
    }

    *modifiers = 0;
    int last_rank = 0;
    for (int i = 0; i < token_count - 1; i++) {
        int rank = 0;
        unsigned char flag = 0;
        if (_stricmp(tokens[i], "Ctrl") == 0) {
            rank = 1; flag = KEY_MOD_CTRL;
        } else if (_stricmp(tokens[i], "Shift") == 0) {
            rank = 2; flag = KEY_MOD_SHIFT;
        } else if (_stricmp(tokens[i], "Alt") == 0) {
            rank = 3; flag = KEY_MOD_ALT;
        } else {
            bindingError(line, L("keybindings.unknown_modifier"), tokens[i]);
        }
        if (rank <= last_rank || (*modifiers & flag))
            bindingError(line, L("keybindings.modifier_order"), value);
        last_rank = rank;
        *modifiers |= flag;
    }

    *virtual_key = virtualKeyFromName(tokens[token_count - 1]);
    if (!*virtual_key)
        bindingError(line, L("keybindings.unknown_key"), tokens[token_count - 1]);
}

static unsigned char modifiersFromState(DWORD state) {
    unsigned char modifiers = 0;
    if (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
        modifiers |= KEY_MOD_CTRL;
    if (state & SHIFT_PRESSED) modifiers |= KEY_MOD_SHIFT;
    if (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
        modifiers |= KEY_MOD_ALT;
    return modifiers;
}

void editorLoadKeyBindings(void) {
    char path[MAX_PATH];
    loggerLog(LOG_INFO, "%s", L("log.keybindings_start"));
    if (!editorExecutablePath(EDITOR_KEY_BINDINGS_FILE, path, sizeof(path)))
        loggerFatal("%s", L("keybindings.path_failed"));
    loggerLog(LOG_INFO, L("log.config_file"), path);

    binding_count = 0;
    int in_bindings = 0;
    struct iniDocument document;
    struct iniError error;
    if (!iniDocumentLoad(path, 512, &document, &error)) {
        if (error.kind == INI_ERROR_OPEN)
            loggerFatal("%s", L("keybindings.open_failed"));
        if (error.kind == INI_ERROR_MISSING_EQUALS)
            bindingError(error.line, L("config.missing_equals"), error.text);
        bindingError(error.line, L("config.unknown_section"), error.text);
    }
    for (int item_index = 0; item_index < document.count; item_index++) {
        struct iniItem *item = &document.items[item_index];
        int line = item->line;
        if (item->type == INI_ITEM_SECTION) {
            in_bindings = _stricmp(item->section, "bindings") == 0;
            if (!in_bindings)
                bindingError(line, L("config.unknown_section"), item->section);
            continue;
        }
        if (!in_bindings)
            bindingError(line, L("keybindings.outside_section"), item->text);

        char *name = item->key;
        char *value = item->value;
        if (!name[0] || !value[0])
            bindingError(line, L("keybindings.empty"), item->text);

        int action = actionFromName(name);
        if (!action) bindingError(line, L("keybindings.unknown_action"), name);
        for (int i = 0; i < binding_count; i++)
            if (bindings[i].action == action)
                bindingError(line, L("keybindings.duplicate_action"), name);

        WORD virtual_key;
        unsigned char modifiers;
        parseBinding(value, &virtual_key, &modifiers, line);
        for (int i = 0; i < binding_count; i++)
            if (bindings[i].virtual_key == virtual_key &&
                bindings[i].modifiers == modifiers)
                bindingError(line, L("keybindings.conflict"), value);

        if (binding_count >= MAX_BINDINGS)
            bindingError(line, L("keybindings.too_many"), value);
        struct keyBinding *binding = &bindings[binding_count++];
        binding->action = action;
        binding->virtual_key = virtual_key;
        binding->modifiers = modifiers;
        snprintf(binding->label, sizeof(binding->label), "%s", value);
        loggerLog(LOG_PASS, L("log.keybinding_line"),
                  line, name, value);
    }
    iniDocumentFree(&document);
    if (binding_count == 0)
        loggerFatal("%s", L("keybindings.none_defined"));
    loggerLog(LOG_INFO, L("log.keybindings_validated"), binding_count);
}

int editorResolveKeyBinding(WORD virtual_key, DWORD control_state) {
    unsigned char modifiers = modifiersFromState(control_state);
    for (int i = 0; i < binding_count; i++)
        if (bindings[i].virtual_key == virtual_key &&
            bindings[i].modifiers == modifiers) {
            int action = bindings[i].action;
            if ((action == ACTION_REPLACE_YES ||
                 action == ACTION_REPLACE_NO ||
                 action == ACTION_REPLACE_ALL) &&
                !E.replace_confirm_active)
                continue;
            return action;
        }
    return NO_KEY_EVENT;
}

const char *editorBindingLabel(int action) {
    for (int i = 0; i < binding_count; i++)
        if (bindings[i].action == action) return bindings[i].label;
    return L("keybindings.unbound");
}

int editorBindingCount(void) {
    return binding_count;
}
