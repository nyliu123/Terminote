/*
 * Complete ascii-banner-c implementation, integrated as a project module.
 * Copyright 2026 lispking. Licensed under Apache-2.0; see
 * licenses/ascii-banner-c.txt.
 */
#define _CRT_SECURE_NO_WARNINGS

#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ascii_banner.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#define FONT_HEIGHT 6
#define MAX_TEXT 4096

typedef struct {
    char character;
    const char *rows[FONT_HEIGHT];
} Glyph;

#include "ascii-banner-font.inc"


static int stdout_is_console(void)
{
#ifdef _WIN32
    DWORD mode;
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    return handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode);
#else
    return 0;
#endif
}

static void output_utf8(const char *text)
{
#ifdef _WIN32
    if (stdout_is_console()) {
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);

        if (needed > 0) {
            wchar_t *wide = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));

            if (wide) {
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, needed) > 0) {
                    DWORD written = 0;
                    WriteConsoleW(handle, wide, (DWORD)(needed - 1), &written, NULL);
                    free(wide);
                    return;
                }

                free(wide);
            }
        }
    }
#endif

    fwrite(text, 1, strlen(text), stdout);
}

static void output_char(char c)
{
    char buffer[2] = {c, '\0'};
    output_utf8(buffer);
}

static void initialize_console(void)
{
    setlocale(LC_ALL, "");
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

static const Glyph *find_glyph(char c)
{
    size_t i;
    unsigned char value = (unsigned char)c;

    if (value >= 'a' && value <= 'z') {
        c = (char)toupper(value);
    }

    for (i = 0; i < FONT_COUNT; ++i) {
        if (FONT[i].character == c) {
            return &FONT[i];
        }
    }

    return NULL;
}

size_t asciiBannerUtf8Width(const char *text)
{
    size_t width = 0;
    const unsigned char *p = (const unsigned char *)text;

    while (*p) {
        if ((*p & 0xC0) != 0x80) {
            ++width;
        }
        ++p;
    }

    return width;
}

static char *append_text(char *destination, size_t capacity, size_t *length, const char *source)
{
    size_t source_length = strlen(source);

    if (*length + source_length + 1 > capacity) {
        return NULL;
    }

    memcpy(destination + *length, source, source_length);
    *length += source_length;
    destination[*length] = '\0';
    return destination;
}

static void glyph_horizontal_padding(const Glyph *glyph, size_t *left, size_t *right)
{
    int row;

    *left = strlen(glyph->rows[0]);
    *right = *left;

    for (row = 0; row < FONT_HEIGHT; ++row) {
        const char *text = glyph->rows[row];
        size_t length = strlen(text);
        size_t leading = 0;
        size_t trailing = 0;

        while (leading < length && text[leading] == ' ') {
            ++leading;
        }
        while (trailing < length && text[length - trailing - 1] == ' ') {
            ++trailing;
        }

        if (leading < *left) {
            *left = leading;
        }
        if (trailing < *right) {
            *right = trailing;
        }
    }
}

static char *append_glyph_row(char *destination, size_t capacity, size_t *length,
                              const char *source, size_t left, size_t right)
{
    size_t source_length = strlen(source);
    size_t content_length;

    if (left > source_length) {
        left = source_length;
    }
    if (right > source_length - left) {
        right = source_length - left;
    }
    content_length = source_length - left - right;

    if (*length + content_length + 1 > capacity) {
        return NULL;
    }

    memcpy(destination + *length, source + left, content_length);
    *length += content_length;
    destination[*length] = '\0';
    return destination;
}

char **asciiBannerTextToRows(const char *text)
{
    size_t text_length = strlen(text);
    size_t row_capacity = text_length * 48 + 1;
    char **rows;
    int row;
    size_t i;

    if (text_length == 0) {
        return NULL;
    }

    rows = (char **)calloc(FONT_HEIGHT, sizeof(char *));
    if (!rows) {
        return NULL;
    }

    for (row = 0; row < FONT_HEIGHT; ++row) {
        rows[row] = (char *)calloc(row_capacity, 1);
        if (!rows[row]) {
            int j;
            for (j = 0; j < row; ++j) {
                free(rows[j]);
            }
            free(rows);
            return NULL;
        }
    }

    for (i = 0; i < text_length; ++i) {
        const Glyph *glyph = find_glyph(text[i]);
        size_t left = 0;
        size_t right = 0;

        if (glyph && glyph->character != ' ') {
            glyph_horizontal_padding(glyph, &left, &right);
        }

        for (row = 0; row < FONT_HEIGHT; ++row) {
            size_t length = strlen(rows[row]);

            if (glyph) {
                if (!append_glyph_row(rows[row], row_capacity, &length,
                                      glyph->rows[row], left, right)) {
                    goto failure;
                }
            } else {
                if (!append_text(rows[row], row_capacity, &length, "   ")) {
                    goto failure;
                }
            }
        }
    }

    for (row = 0; row < FONT_HEIGHT; ++row) {
        size_t length = strlen(rows[row]);

        while (length > 0 && rows[row][length - 1] == ' ') {
            rows[row][--length] = '\0';
        }
    }

    if (rows[0][0] == '\0') {
        goto failure;
    }

    return rows;

failure:
    for (row = 0; row < FONT_HEIGHT; ++row) {
        free(rows[row]);
    }
    free(rows);
    return NULL;
}

void asciiBannerFreeRows(char **rows)
{
    int row;

    if (!rows) {
        return;
    }

    for (row = 0; row < FONT_HEIGHT; ++row) {
        free(rows[row]);
    }

    free(rows);
}

static void print_repeat(const char *text, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        output_utf8(text);
    }
}

static int print_banner(const char *text, size_t padding, int with_box)
{
    char **rows = asciiBannerTextToRows(text);
    size_t max_width = 0;
    size_t inner_width;
    int row;

    if (!rows) {
        fprintf(stderr, "Error: Unable to convert text\n");
        return 1;
    }

    if (!with_box) {
        for (row = 0; row < FONT_HEIGHT; ++row) {
            output_utf8(rows[row]);
            output_char('\n');
        }
        asciiBannerFreeRows(rows);
        return 0;
    }

    for (row = 0; row < FONT_HEIGHT; ++row) {
        size_t width = asciiBannerUtf8Width(rows[row]);
        if (width > max_width) {
            max_width = width;
        }
    }

    inner_width = max_width + padding * 2;

    output_utf8("\xE2\x95\x94");
    print_repeat("\xE2\x95\x90", inner_width + 2);
    output_utf8("\xE2\x95\x97\n");

    for (row = 0; row < (int)padding; ++row) {
        output_utf8("\xE2\x95\x91 ");
        print_repeat(" ", inner_width);
        output_utf8(" \xE2\x95\x91\n");
    }

    for (row = 0; row < FONT_HEIGHT; ++row) {
        size_t width = asciiBannerUtf8Width(rows[row]);

        output_utf8("\xE2\x95\x91 ");
        print_repeat(" ", padding);
        output_utf8(rows[row]);
        print_repeat(" ", inner_width - padding - width);
        output_utf8(" \xE2\x95\x91\n");
    }

    for (row = 0; row < (int)padding; ++row) {
        output_utf8("\xE2\x95\x91 ");
        print_repeat(" ", inner_width);
        output_utf8(" \xE2\x95\x91\n");
    }

    output_utf8("\xE2\x95\x9A");
    print_repeat("\xE2\x95\x90", inner_width + 2);
    output_utf8("\xE2\x95\x9D\n");

    asciiBannerFreeRows(rows);
    return 0;
}

static void print_help(void)
{
    puts("ASCII Banner - ASCII Art Text Generator");
    puts("");
    puts("Usage:");
    puts("  ascii-banner <text>           Generate ASCII art with border");
    puts("  ascii-banner <text> --no-box  Generate ASCII art without border");
    puts("  ascii-banner --help           Show this help message");
    puts("");
    puts("Arguments:");
    puts("  <text>    Text to convert (supports A-Z)");
    puts("  --no-box  Don't display border");
    puts("  --help    Show this help message");
    puts("");
    puts("Examples:");
    puts("  ascii-banner \"HELLO\"");
    puts("  ascii-banner \"C 2026\" --no-box");
}

int asciiBannerCliMain(int argc, char **argv)
{
    char text[MAX_TEXT] = {0};
    size_t length = 0;
    int no_box = 0;
    int i;

    initialize_console();

    if (argc < 2) {
        print_help();
        return 0;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "/?") == 0) {
            print_help();
            return 0;
        }

        if (strcmp(argv[i], "--no-box") == 0) {
            no_box = 1;
            continue;
        }

        if (length > 0) {
            if (!append_text(text, sizeof(text), &length, " ")) {
                fputs("Error: Input is too long\n", stderr);
                return 1;
            }
        }

        if (!append_text(text, sizeof(text), &length, argv[i])) {
            fputs("Error: Input is too long\n", stderr);
            return 1;
        }
    }

    if (text[0] == '\0') {
        fputs("Error: Please enter text to convert\n", stderr);
        return 1;
    }

    return print_banner(text, 1, !no_box);
}
