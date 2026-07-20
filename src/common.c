#include "common.h"

#include "colors.h"
#include "syntax.h"

/* ---- global state ---- */
struct editorConfig E;

struct undoRecord *undo_stack = NULL;
int undo_count = 0;
int undo_cap   = 0;

struct undoRecord *redo_stack = NULL;
int redo_count = 0;
int redo_cap   = 0;

int undo_suspended = 0;

/* ---- encoding helpers ---- */
int is_utf8_cont(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

int utf8_seq_len(unsigned char c) {
    if (c < 0x80)       return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/* keep old name for compatibility — calls above */
int utf8_char_len(unsigned char c) {
    return utf8_seq_len(c);
}

int utf8_display_width(const char *s) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return 1;
    return 2;
}

int utf8_terminal_char_width(const char *text, int *byte_length) {
    const unsigned char *bytes = (const unsigned char *)text;
    int length = utf8_seq_len(bytes[0]);
    unsigned int codepoint;

    if (length == 1) {
        *byte_length = 1;
        return 1;
    }
    for (int i = 1; i < length; i++) {
        if (!bytes[i] || !is_utf8_cont(bytes[i])) {
            *byte_length = 1;
            return 1;
        }
    }

    if (length == 2)
        codepoint = ((unsigned int)(bytes[0] & 0x1f) << 6) |
                    (unsigned int)(bytes[1] & 0x3f);
    else if (length == 3)
        codepoint = ((unsigned int)(bytes[0] & 0x0f) << 12) |
                    ((unsigned int)(bytes[1] & 0x3f) << 6) |
                    (unsigned int)(bytes[2] & 0x3f);
    else
        codepoint = ((unsigned int)(bytes[0] & 0x07) << 18) |
                    ((unsigned int)(bytes[1] & 0x3f) << 12) |
                    ((unsigned int)(bytes[2] & 0x3f) << 6) |
                    (unsigned int)(bytes[3] & 0x3f);
    *byte_length = length;

    if ((codepoint >= 0x0300 && codepoint <= 0x036f) ||
        (codepoint >= 0x1ab0 && codepoint <= 0x1aff) ||
        (codepoint >= 0x1dc0 && codepoint <= 0x1dff) ||
        (codepoint >= 0x20d0 && codepoint <= 0x20ff) ||
        (codepoint >= 0xfe20 && codepoint <= 0xfe2f))
        return 0;
    if ((codepoint >= 0x1100 && codepoint <= 0x115f) ||
        codepoint == 0x2329 || codepoint == 0x232a ||
        (codepoint >= 0x2e80 && codepoint <= 0xa4cf &&
         codepoint != 0x303f) ||
        (codepoint >= 0xac00 && codepoint <= 0xd7a3) ||
        (codepoint >= 0xf900 && codepoint <= 0xfaff) ||
        (codepoint >= 0xfe10 && codepoint <= 0xfe19) ||
        (codepoint >= 0xfe30 && codepoint <= 0xfe6f) ||
        (codepoint >= 0xff00 && codepoint <= 0xff60) ||
        (codepoint >= 0xffe0 && codepoint <= 0xffe6) ||
        (codepoint >= 0x1f300 && codepoint <= 0x1faff) ||
        (codepoint >= 0x20000 && codepoint <= 0x3fffd))
        return 2;
    return 1;
}

int utf8_terminal_width(const char *text) {
    int width = 0;
    while (*text) {
        int length;
        width += utf8_terminal_char_width(text, &length);
        text += length;
    }
    return width;
}

/* ---- walk backward to the start of a UTF-8 character ---- */
int editorPrevCharByte(erow *row, int at) {
    if (at <= 0) return 0;
    int i = at - 1;
    while (i > 0 && is_utf8_cont((unsigned char)row->chars[i]))
        i--;
    return i;
}

int editorNextCharByte(erow *row, int at) {
    if (at >= row->size) return row->size;
    int len = utf8_seq_len((unsigned char)row->chars[at]);
    if (at + len > row->size) len = row->size - at;
    return at + len;
}

/* ---- utilities ---- */
void die(const char *s) {
    SetConsoleTextAttribute(E.hout, 7);
    system("cls");
    fprintf(stderr, "%s\n", s);
    exit(1);
}

char *editor_strdup(const char *s) {
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (!p) die("malloc");
    memcpy(p, s, len + 1);
    return p;
}

int is_separator(int c) {
    unsigned char value = (unsigned char)c;
    return value == '\0' || isspace(value) ||
           (!isalnum(value) && value != '_');
}

int editorSyntaxToColor(int hl) {
    if (hl >= HL_LANGUAGE_BASE) return editorSyntaxKeywordColor(hl);
    switch (hl) {
        case HL_COMMENT:  return editorColorCode(COLOR_SYNTAX_COMMENT);
        case HL_STRING:   return editorColorCode(COLOR_SYNTAX_STRING);
        case HL_NUMBER:   return editorColorCode(COLOR_SYNTAX_NUMBER);
        case HL_VARIABLE: return editorColorCode(COLOR_SYNTAX_VARIABLE);
        case HL_FUNCTION: return editorColorCode(COLOR_SYNTAX_FUNCTION);
        case HL_SECTION:  return editorSyntaxSectionColor();
        case HL_MATCH:    return editorColorCode(COLOR_MATCH);
        default:          return editorColorCode(COLOR_TEXT);
    }
}
