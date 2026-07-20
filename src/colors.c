#include "common.h"
#include "colors.h"

char editor_colors[COLOR_COUNT][32];
static int editor_color_codes[COLOR_COUNT];

struct ansiColorName {
    const char *name;
    enum ansiColor color;
};

static const struct ansiColorName ansi_color_names[] = {
    {"RESET", ANSI_RESET}, {"BOLD", ANSI_BOLD}, {"DIM", ANSI_DIM},
    {"ITALIC", ANSI_ITALIC}, {"UNDERLINE", ANSI_UNDERLINE},
    {"REVERSE", ANSI_REVERSE}, {"STRIKETHROUGH", ANSI_STRIKETHROUGH},
    {"BLACK", ANSI_BLACK}, {"RED", ANSI_RED}, {"GREEN", ANSI_GREEN},
    {"YELLOW", ANSI_YELLOW}, {"BLUE", ANSI_BLUE},
    {"MAGENTA", ANSI_MAGENTA}, {"CYAN", ANSI_CYAN},
    {"WHITE", ANSI_WHITE}, {"DEFAULT_TEXT", ANSI_DEFAULT_TEXT},
    {"BG_BLACK", ANSI_BG_BLACK}, {"BG_RED", ANSI_BG_RED},
    {"BG_GREEN", ANSI_BG_GREEN}, {"BG_YELLOW", ANSI_BG_YELLOW},
    {"BG_BLUE", ANSI_BG_BLUE}, {"BG_MAGENTA", ANSI_BG_MAGENTA},
    {"BG_CYAN", ANSI_BG_CYAN}, {"BG_WHITE", ANSI_BG_WHITE},
    {"DEFAULT_BACKGROUND", ANSI_DEFAULT_BACKGROUND},
    {"BRIGHT_BLACK", ANSI_BRIGHT_BLACK},
    {"BRIGHT_RED", ANSI_BRIGHT_RED},
    {"BRIGHT_GREEN", ANSI_BRIGHT_GREEN},
    {"BRIGHT_YELLOW", ANSI_BRIGHT_YELLOW},
    {"BRIGHT_BLUE", ANSI_BRIGHT_BLUE},
    {"BRIGHT_MAGENTA", ANSI_BRIGHT_MAGENTA},
    {"BRIGHT_CYAN", ANSI_BRIGHT_CYAN},
    {"BRIGHT_WHITE", ANSI_BRIGHT_WHITE},
    {"BG_BRIGHT_BLACK", ANSI_BG_BRIGHT_BLACK},
    {"BG_BRIGHT_RED", ANSI_BG_BRIGHT_RED},
    {"BG_BRIGHT_GREEN", ANSI_BG_BRIGHT_GREEN},
    {"BG_BRIGHT_YELLOW", ANSI_BG_BRIGHT_YELLOW},
    {"BG_BRIGHT_BLUE", ANSI_BG_BRIGHT_BLUE},
    {"BG_BRIGHT_MAGENTA", ANSI_BG_BRIGHT_MAGENTA},
    {"BG_BRIGHT_CYAN", ANSI_BG_BRIGHT_CYAN},
    {"BG_BRIGHT_WHITE", ANSI_BG_BRIGHT_WHITE},
    {"BG_NEAR_BLACK", ANSI_BG_NEAR_BLACK},
    {"BG_LIGHT_GRAY", ANSI_BG_LIGHT_GRAY},
    {NULL, ANSI_RESET}
};

void editorInitializeColors(void) {
    for (int slot = 0; slot < COLOR_COUNT; slot++) {
        editor_color_codes[slot] = slot == COLOR_RESET
            ? ANSI_RESET : ANSI_DEFAULT_TEXT;
        snprintf(editor_colors[slot], sizeof(editor_colors[slot]),
                 "\033[%dm", editor_color_codes[slot]);
    }
}

const char *editorColorSequenceForCode(int color) {
    static char sequence[32];
    switch (color) {
        case ANSI_BG_NEAR_BLACK: return "\033[48;5;235m";
        case ANSI_BG_LIGHT_GRAY: return "\033[48;2;128;128;128m";
        default:
            snprintf(sequence, sizeof(sequence), "\033[%dm", color);
            return sequence;
    }
}

void editorSetColor(enum editorColorSlot slot, enum ansiColor color) {
    if (slot < 0 || slot >= COLOR_COUNT) return;
    editor_color_codes[slot] = color;
    snprintf(editor_colors[slot], sizeof(editor_colors[slot]), "%s",
             editorColorSequenceForCode(color));
}

const char *editorColor(enum editorColorSlot slot) {
    if (slot < 0 || slot >= COLOR_COUNT) return "\033[0m";
    return editor_colors[slot];
}

int editorColorCode(enum editorColorSlot slot) {
    if (slot < 0 || slot >= COLOR_COUNT) return 39;
    return editor_color_codes[slot];
}

int editorColorFromName(const char *name, enum ansiColor *color) {
    for (int i = 0; ansi_color_names[i].name; i++) {
        if (_stricmp(name, ansi_color_names[i].name) == 0) {
            *color = ansi_color_names[i].color;
            return 1;
        }
    }
    return 0;
}
