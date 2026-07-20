#include "common.h"
#include "terminal.h"
#include "row.h"
#include "output.h"
#include "editor.h"
#include "prompt.h"
#include "mouse.h"

static int promptDisplayWidth(const char *text, int byte_count) {
    int width = 0;
    int at = 0;
    while (at < byte_count && text[at]) {
        unsigned char byte = (unsigned char)text[at];
        int length = utf8_seq_len(byte);
        if (at + length > byte_count) length = byte_count - at;
        width += byte < 0x80 ? 1 : 2;
        at += length;
    }
    return width;
}

static int promptPrefixWidth(const char *format) {
    const char *placeholder = strstr(format, "%s");
    int prefix_bytes = placeholder ? (int)(placeholder - format)
                                   : (int)strlen(format);
    return promptDisplayWidth(format, prefix_bytes);
}

static int promptBytePositionAtColumn(const char *text, int length,
                                      int target_column) {
    int byte_pos = 0;
    int column = 0;
    if (target_column <= 0) return 0;

    while (byte_pos < length) {
        unsigned char byte = (unsigned char)text[byte_pos];
        int char_len = utf8_seq_len(byte);
        int char_width = byte < 0x80 ? 1 : 2;
        if (byte_pos + char_len > length) char_len = length - byte_pos;
        if (target_column < column + char_width) break;
        column += char_width;
        byte_pos += char_len;
    }
    return byte_pos;
}

struct promptBuffer {
    char *text;
    size_t length, capacity;
    int cursor;
};

static void promptBufferInit(struct promptBuffer *input) {
    input->capacity = 128;
    input->length = input->cursor = 0;
    input->text = malloc(input->capacity);
    if (!input->text) die("malloc");
    input->text[0] = '\0';
}

static void promptDelete(struct promptBuffer *input, int backward) {
    if (backward && input->cursor <= 0) return;
    if (!backward && input->cursor >= (int)input->length) return;

    int start = input->cursor;
    if (backward) {
        start--;
        while (start > 0 && is_utf8_cont((unsigned char)input->text[start]))
            start--;
    }
    int end = backward ? input->cursor : start +
        utf8_seq_len((unsigned char)input->text[start]);
    if (end > (int)input->length) end = (int)input->length;
    memmove(&input->text[start], &input->text[end], input->length - end);
    input->length -= end - start;
    input->cursor = start;
    input->text[input->length] = '\0';
}

static void promptMove(struct promptBuffer *input, int direction) {
    if (direction < 0 && input->cursor > 0) {
        input->cursor--;
        while (input->cursor > 0 &&
               is_utf8_cont((unsigned char)input->text[input->cursor]))
            input->cursor--;
    } else if (direction > 0 && input->cursor < (int)input->length) {
        int length = utf8_seq_len((unsigned char)input->text[input->cursor]);
        input->cursor += input->cursor + length <= (int)input->length
            ? length : 1;
    }
}

static void promptInsert(struct promptBuffer *input, int byte) {
    if (input->length + 1 >= input->capacity) {
        input->capacity *= 2;
        input->text = realloc(input->text, input->capacity);
        if (!input->text) die("realloc");
    }
    memmove(&input->text[input->cursor + 1], &input->text[input->cursor],
            input->length - input->cursor + 1);
    input->text[input->cursor++] = (char)byte;
    input->length++;
}

static int promptEdit(struct promptBuffer *input, int key) {
    if (key == ACTION_DELETE_BACKWARD || key == ACTION_DELETE_FORWARD)
        promptDelete(input, key == ACTION_DELETE_BACKWARD);
    else if (key == ACTION_MOVE_LEFT || key == ACTION_MOVE_RIGHT)
        promptMove(input, key == ACTION_MOVE_LEFT ? -1 : 1);
    else if (key == ACTION_MOVE_TO_LINE_START) input->cursor = 0;
    else if (key == ACTION_MOVE_TO_LINE_END) input->cursor = (int)input->length;
    else if (key >= 32 && key <= 255) promptInsert(input, key);
    else return 0;
    return 1;
}

static char *finishPrompt(struct promptBuffer *input,
                          void (*callback)(char *, int), int action,
                          int accept) {
    editorSetStatusMessage("");
    if (callback) callback(input->text, action);
    E.prompt_active = 0;
    if (accept) return input->text;
    free(input->text);
    return NULL;
}

/* ---- prompt / input line ---- */
static char *editorPromptImpl(char *prompt, void (*callback)(char *, int),
                              int cancel_on_outside_click) {
    struct promptBuffer input;
    promptBufferInit(&input);
    E.prompt_active = 1;

    /* draw initial prompt before waiting for first keypress */
    editorSetStatusMessage(prompt, input.text);
    editorRefreshScreen();
    { DWORD w; WriteConsoleA(E.hout, "\x1b[?25l", 6, &w, NULL); }

    while (1) {
        int c = editorReadKey();
        int skip_callback = 0;

        if (promptEdit(&input, c)) {
            /* Text editing is complete; callback and redraw happen below. */
        } else switch (c) {
        case ACTION_PAGE_UP:
        case ACTION_PAGE_DOWN:
            editorHandleModalViewInput(c, 0);
            break;

        case MOUSE_PRESS:
            /* event line click --cursor positioning */
            if (E.mouse_y == E.screenrows + 1) {
                int input_left = promptPrefixWidth(prompt);
                int target_column = E.mouse_x - input_left;
                input.cursor = promptBytePositionAtColumn(
                    input.text, (int)input.length, target_column);
                break;
            }

            if (editorHandleModalViewInput(c, 0)) {
                skip_callback = 1;
                break;
            }

            if (cancel_on_outside_click) {
                return finishPrompt(&input, callback, ACTION_CANCEL, 0);
            }

            /* Non-search prompts retain the original click-to-confirm behavior. */
            return finishPrompt(&input, input.length ? callback : NULL,
                                ACTION_INSERT_NEWLINE, input.length != 0);

        case MOUSE_DRAG:
            skip_callback = E.hscroll_dragging;
            editorHandleModalViewInput(c, 0);
            break;
        case MOUSE_RELEASE:
        case WHEEL_UP:
        case WHEEL_DOWN:
            editorHandleModalViewInput(c, 0);
            skip_callback = 1;
            break;

        case ACTION_CANCEL:
            return finishPrompt(&input, callback, c, 0);

        case ACTION_INSERT_NEWLINE:
            if (input.length != 0) return finishPrompt(&input, callback, c, 1);
            break;
        }

        /* let callback override the status message */
        if (!skip_callback) {
            E.statusmsg_dirty = 0;
            if (callback) callback(input.text, c);
        }

        /* if callback didn't set a status msg (or was skipped),
           use the prompt default */
        if (!E.statusmsg_dirty)
            editorSetStatusMessage(prompt, input.text);

        editorRefreshScreen();

        /* cursor visibility after refresh */
        {
            if (input.length == 0) {
                DWORD w;
                WriteConsoleA(E.hout, "\x1b[?25l", 6, &w, NULL);
            } else {
                DWORD w;
                WriteConsoleA(E.hout, "\x1b[?25h", 6, &w, NULL);
                int prefix_width = promptPrefixWidth(prompt);
                int cursor_width = promptDisplayWidth(input.text, input.cursor);
                char  cur_buf[32];
                snprintf(cur_buf, sizeof(cur_buf), "\x1b[%d;%dH",
                         E.screenrows + 2,
                         prefix_width + cursor_width + 1);
                WriteConsoleA(E.hout, cur_buf, (int)strlen(cur_buf), &w, NULL);
            }
        }
    }
}

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    return editorPromptImpl(prompt, callback, 0);
}

char *editorSearchPrompt(char *prompt, void (*callback)(char *, int)) {
    return editorPromptImpl(prompt, callback, 1);
}
