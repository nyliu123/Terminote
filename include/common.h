#ifndef COMMON_H
#define COMMON_H

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <conio.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <limits.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#define EDITOR_VERSION "1.0.0"
#define EDITOR_QUIT_TIMES 3
/* ---- input events and configured actions ---- */
enum editorKey {
    NO_KEY_EVENT = -1,
    WHEEL_UP = 1000,
    WHEEL_DOWN,
    MOUSE_PRESS,
    MOUSE_DRAG,
    MOUSE_RELEASE,
    MOUSE_CLICK,
    MOUSE_HOVER,
    WINDOW_RESIZED,
    PASTE_EVENT,

    ACTION_SAVE = 2000,
    ACTION_SAVE_AS,
    ACTION_OPEN,
    ACTION_NEW_FILE,
    ACTION_NEXT_TAB,
    ACTION_RUN,
    ACTION_SETTINGS,
    ACTION_QUIT_WITHOUT_SAVING,
    ACTION_CUT,
    ACTION_COPY,
    ACTION_PASTE,
    ACTION_SELECT_ALL,
    ACTION_FIND,
    ACTION_FIND_AND_REPLACE,
    ACTION_UNDO,
    ACTION_REDO,
    ACTION_HELP,
    ACTION_CANCEL,
    ACTION_PLACEHOLDER,
    ACTION_INSERT_NEWLINE,
    ACTION_INDENT,
    ACTION_UNINDENT,
    ACTION_DELETE_BACKWARD,
    ACTION_DELETE_FORWARD,
    ACTION_MOVE_UP,
    ACTION_MOVE_DOWN,
    ACTION_MOVE_LEFT,
    ACTION_MOVE_RIGHT,
    ACTION_MOVE_TO_LINE_START,
    ACTION_MOVE_TO_LINE_END,
    ACTION_PAGE_UP,
    ACTION_PAGE_DOWN,
    ACTION_REPLACE_YES,
    ACTION_REPLACE_NO,
    ACTION_REPLACE_ALL
};

/* ---- syntax highlight types ---- */
enum editorHighlight {
    HL_NORMAL = 0,
    HL_NUMBER,
    HL_MATCH,
    HL_STRING,
    HL_COMMENT,
    HL_VARIABLE,
    HL_FUNCTION,
    HL_SECTION,
    HL_LANGUAGE_BASE = 16
};

/* ---- undo operation types ---- */
enum {
    UO_INSERT_CHAR = 1,
    UO_DELETE_CHAR,
    UO_SPLIT_ROW,
    UO_JOIN_ROW,
    UO_INSERT_ROW,
    UO_DELETE_ROW,
    UO_INSERT_BLOCK,
    UO_DELETE_BLOCK,
    UO_INDENT_LINES,
    UO_UNINDENT_LINES,
    UO_REPLACE_BLOCK,
    UO_MOVE_BLOCK,
    UO_ATOMIC_INSERT,
    UO_ATOMIC_DELETE,
    UO_ATOMIC_REPLACE,
    UO_ATOMIC_MOVE
};

/* ---- data structures ---- */
typedef struct erow {
    int idx;
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *hl;
    int hl_open_state;
} erow;

struct undoRecord {
    int op_type;
    int group_id;
    int row, col;
    int cursor_row, cursor_col;
    char save[4];
    int save_len;
    /* for text/block undo records: the primary text */
    char *block_text;
    int  block_len;
    int  end_row, end_col;
    /* for move records: insertion point after original delete */
    int  dest_row, dest_col;
    /* for replace records: the inserted/replacement text */
    char *new_text;
    int  new_len;
    int  new_end_row, new_end_col;
    /* optional cursor position after replaying an atomic insertion */
    int  result_row, result_col;
};

struct editorConfig {
    int cx, cy;
    int rx;
    int rowoff, coloff;
    int screenrows, screencols;
    int numrows;
    erow *row;
    int dirty;
    char *filename;
    char statusmsg[256];
    int statusmsg_dirty;
    int prompt_active;
    int search_active;
    int replace_confirm_active;
    int welcome_dismissed;   /* set once user makes first edit */
    char cur_event[256];     /* current event shown below scrollbar */
    ULONGLONG input_activity_deadline;
    int ready_timeout_ms;
    int auto_indent;
    int auto_pair;
    int code_completion;
    int bracket_matching;
    int tab_width;
    int save_failed;
    int mouse_x, mouse_y;
    int linenum_width;
    int sel_anchor_row, sel_anchor_col;
    int sel_active;
    int mouse_selecting;
    int wheel_scrolling;
    int hscroll_dragging;
    int hscroll_start_coloff;
    int hscroll_start_mx;
    int auto_scroll_h;   /* -1=left, 0=none, 1=right */
    int auto_scroll_v;   /* -1=up,   0=none, 1=down  */
    int mouse_drag_move; /* 1 while drag-moving selected text */
    int drag_sel_sr, drag_sel_sc; /* saved selection start */
    int drag_sel_er, drag_sel_ec; /* saved selection end   */
    char *drag_text;              /* copy of selected text  */
    int  drag_text_len;
    HANDLE hin, hout;
    DWORD original_in_mode, original_out_mode;
};

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

struct findMatch {
    int row;
    int col;
};

/* ---- global state ---- */
extern struct editorConfig E;

extern struct undoRecord *undo_stack;
extern int undo_count;
extern int undo_cap;

extern struct undoRecord *redo_stack;
extern int redo_count;
extern int redo_cap;

extern int undo_suspended;

/* ---- encoding helpers ---- */
int  is_utf8_cont(unsigned char c);
int  utf8_seq_len(unsigned char c);
int  utf8_char_len(unsigned char c);
int  utf8_display_width(const char *s);
int  utf8_terminal_char_width(const char *text, int *byte_length);
int  utf8_terminal_width(const char *text);
int  editorPrevCharByte(erow *row, int at);
int  editorNextCharByte(erow *row, int at);

/* ---- general utilities ---- */
void die(const char *s);
char *editor_strdup(const char *s);
int  is_separator(int c);
int  editorSyntaxToColor(int hl);

#endif
