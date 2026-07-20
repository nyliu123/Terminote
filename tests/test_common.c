#include <assert.h>
#include <stdio.h>

#include "common.h"
#include "colors.h"

int editorColorCode(enum editorColorSlot slot) {
    return (int)slot;
}

int editorSyntaxKeywordColor(int highlight) {
    return highlight;
}

int editorSyntaxSectionColor(void) {
    return 0;
}

static void testUtf8Helpers(void) {
    assert(utf8_seq_len('A') == 1);
    assert(utf8_seq_len(0xc2) == 2);
    assert(utf8_seq_len(0xe4) == 3);
    assert(utf8_seq_len(0xf0) == 4);
    assert(is_utf8_cont(0x80));
    assert(is_utf8_cont(0xbf));
    assert(!is_utf8_cont(0xc0));
    assert(utf8_terminal_width("é") == 1);
    assert(utf8_terminal_width("Настройки") == 9);
    assert(utf8_terminal_width("设置") == 4);
    assert(utf8_terminal_width("e\xcc\x81") == 1);
}

static void testUtf8CursorBoundaries(void) {
    char text[] = "a中b";
    erow row = {0};
    row.chars = text;
    row.size = (int)strlen(text);

    assert(editorNextCharByte(&row, 0) == 1);
    assert(editorNextCharByte(&row, 1) == 4);
    assert(editorNextCharByte(&row, 4) == 5);
    assert(editorPrevCharByte(&row, 5) == 4);
    assert(editorPrevCharByte(&row, 4) == 1);
    assert(editorPrevCharByte(&row, 1) == 0);
}

static void testSeparators(void) {
    assert(is_separator('\0'));
    assert(is_separator(' '));
    assert(is_separator('+'));
    assert(!is_separator('_'));
    assert(!is_separator('a'));
}

int main(void) {
    testUtf8Helpers();
    testUtf8CursorBoundaries();
    testSeparators();
    puts("test_common: PASS");
    return 0;
}
