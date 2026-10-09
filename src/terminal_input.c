#include "common.h"
#include "win_text.h"
#include "terminal.h"
#include "row.h"
#include "output.h"
#include "keybindings.h"

static char *pending_paste = NULL;
static int pending_paste_len = 0;
static int next_tab_shortcut_latched = 0;

static int editorConsoleIsForeground(void) {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return 0;
    if (foreground == GetConsoleWindow()) return 1;
    char class_name[128] = "";
    GetClassNameA(foreground, class_name, sizeof(class_name));
    return strcmp(class_name, "CASCADIA_HOSTING_WINDOW_CLASS") == 0;
}

int editorPollGlobalShortcut(void) {
    int control_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    int space_down = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    int down = editorConsoleIsForeground() && control_down && space_down;
    if (!down) {
        next_tab_shortcut_latched = 0;
        return NO_KEY_EVENT;
    }
    if (next_tab_shortcut_latched) return NO_KEY_EVENT;
    next_tab_shortcut_latched = 1;
    return ACTION_NEXT_TAB;
}

static int editorKeyTextByte(const KEY_EVENT_RECORD *key, wchar_t *out) {
    DWORD modifiers = key->dwControlKeyState;
    if (!key->bKeyDown) return -1;
    if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED |
                     LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
        return 0;
    if (key->wVirtualKeyCode == VK_RETURN) {
        *out = '\r';
        return 1;
    }
    if (key->wVirtualKeyCode == VK_TAB) {
        *out = '\t';
        return 1;
    }
    if (key->uChar.UnicodeChar != 0 &&
        key->uChar.UnicodeChar >= 32) {
        *out = key->uChar.UnicodeChar;
        return 1;
    }
    return 0;
}

static int editorKeyIsSyntheticText(const KEY_EVENT_RECORD *key) {
    return key->wVirtualKeyCode == 0 || key->wVirtualKeyCode == VK_PACKET;
}

/* Preserve a surrogate pair even when its console records arrive separately. */
static void editorQueueWideText(const wchar_t *text) {
    static wchar_t high_surrogate = 0;
    size_t length = wcslen(text);
    wchar_t *complete = malloc((length + 2) * sizeof(*complete));
    if (!complete) die("malloc: console text");
    size_t at = 0;
    if (high_surrogate && length && text[0] >= 0xdc00 && text[0] <= 0xdfff)
        complete[at++] = high_surrogate;
    high_surrogate = 0;
    memcpy(complete + at, text, (length + 1) * sizeof(*text));
    length += at;
    if (length && complete[length - 1] >= 0xd800 && complete[length - 1] <= 0xdbff) {
        high_surrogate = complete[--length];
        complete[length] = L'\0';
    }
    pending_paste = editorWideToUtf8(complete);
    pending_paste_len = pending_paste ? (int)strlen(pending_paste) : 0;
    free(complete);
}
static int editorCollectPendingPaste(const INPUT_RECORD *first) {
    DWORD available = 0, peeked = 0, consumed = 0;
    INPUT_RECORD *records = NULL;
    wchar_t first_byte;
    int first_count, text_len, prefix = 0;

    if (editorKeyTextByte(&first->Event.KeyEvent, &first_byte) != 1) return 0;
    first_count = first->Event.KeyEvent.wRepeatCount;
    if (first_count < 1) first_count = 1;
    text_len = first_count;

    int synthetic = editorKeyIsSyntheticText(&first->Event.KeyEvent);

    /* A repeated physical key is one input record containing several copies.
       Keep it atomic without merging unrelated queued keyboard events. */
    if (!synthetic && first_count > 1) {
        free(pending_paste);
        wchar_t *wide = malloc((first_count + 1) * sizeof(*wide));
        if (!wide) die("malloc: repeated key text");
        for (int i = 0; i < first_count; i++) wide[i] = first_byte;
        wide[first_count] = L'\0';
        editorQueueWideText(wide);
        free(wide);
        return 1;
    }

    if (!GetNumberOfConsoleInputEvents(E.hin, &available) || available == 0) {
        if (first_count <= 1 && first_byte < 128) return 0;
        free(pending_paste);
        wchar_t *wide = malloc((first_count + 1) * sizeof(*wide));
        if (!wide) die("malloc: pending paste");
        for (int i = 0; i < first_count; i++) wide[i] = first_byte;
        wide[first_count] = L'\0';
        editorQueueWideText(wide);
        free(wide);
        return 1;
    }

    records = malloc(sizeof(INPUT_RECORD) * available);
    if (!records) die("malloc: pending paste records");
    if (!PeekConsoleInputW(E.hin, records, available, &peeked)) {
        free(records);
        return 0;
    }

    for (DWORD i = 0; i < peeked; i++) {
        wchar_t byte;
        if (records[i].EventType != KEY_EVENT) break;
        if (!records[i].Event.KeyEvent.bKeyDown) {
            /* Physical typing normally alternates key-down and key-up.  A
               key-up before a second text event proves this is not a paste.
               Terminal paste bursts instead arrive as synthetic events or a
               contiguous run of text key-down records. */
            if (!synthetic) {
                if (text_len <= 1 && first_byte < 128) {
                    free(records);
                    return 0;
                }
                break;
            }
            prefix++;
            continue;
        }
        int kind = editorKeyTextByte(&records[i].Event.KeyEvent, &byte);
        if (kind == 0) break;
        prefix++;
        if (kind == 1) {
            int repeats = records[i].Event.KeyEvent.wRepeatCount;
            if (repeats < 1) repeats = 1;
            text_len += repeats;
        }
    }

    if (text_len <= 1 && first_byte < 128) {
        free(records);
        return 0;
    }

    free(pending_paste);
    wchar_t *wide = malloc((text_len + 1) * sizeof(*wide));
    if (!wide) die("malloc: pending paste");
    int at = 0;
    for (int i = 0; i < first_count; i++) wide[at++] = first_byte;
    for (int i = 0; i < prefix; i++) {
        wchar_t byte;
        if (editorKeyTextByte(&records[i].Event.KeyEvent, &byte) == 1) {
            int repeats = records[i].Event.KeyEvent.wRepeatCount;
            if (repeats < 1) repeats = 1;
            while (repeats-- > 0) wide[at++] = byte;
        }
    }
    wide[at] = L'\0';
    editorQueueWideText(wide);
    free(wide);

    if (prefix > 0)
        ReadConsoleInputW(E.hin, records, (DWORD)prefix, &consumed);
    free(records);
    return 1;
}

char *editorTakePendingPaste(int *len) {
    char *text = pending_paste;
    *len = pending_paste_len;
    pending_paste = NULL;
    pending_paste_len = 0;
    return text;
}

static int editorReadMouseEvent(const MOUSE_EVENT_RECORD *mouse) {
    DWORD flags = mouse->dwEventFlags;
    DWORD buttons = mouse->dwButtonState;
    if (flags == MOUSE_WHEELED) {
        int delta = (int)(short)HIWORD(buttons);
        return delta > 0 ? WHEEL_UP : WHEEL_DOWN;
    }

    E.mouse_x = mouse->dwMousePosition.X;
    E.mouse_y = mouse->dwMousePosition.Y;
    if (flags == 0 && (buttons & FROM_LEFT_1ST_BUTTON_PRESSED)) return MOUSE_PRESS;
    if (flags == MOUSE_MOVED && (buttons & FROM_LEFT_1ST_BUTTON_PRESSED)) return MOUSE_DRAG;
    if (flags == 0 && !(buttons & FROM_LEFT_1ST_BUTTON_PRESSED)) return MOUSE_RELEASE;
    if (flags == MOUSE_MOVED && !(buttons & FROM_LEFT_1ST_BUTTON_PRESSED)) return MOUSE_HOVER;
    return 0;
}

static void editorHandleResize(void) {
    int rows, cols;
    if (getWindowSize(&rows, &cols) != 0) return;
    editorFitBufferToWindow();
    editorUpdateLineNumWidth();
    hideConsoleScrollbars();

    int visible_rows = editorVisibleRows();
    if (visible_rows < 1) visible_rows = 1;
    if (E.cy < E.rowoff) E.rowoff = E.cy;
    if (E.cy >= E.rowoff + visible_rows)
        E.rowoff = E.cy - visible_rows + 1;
    if (E.rowoff < 0) E.rowoff = 0;
}

static int editorReadInputEvent(int blocking) {
    INPUT_RECORD record;
    DWORD count;

    for (;;) {
        if (!blocking) {
            DWORD available = 0;
            if (!GetNumberOfConsoleInputEvents(E.hin, &available) ||
                available == 0)
                return NO_KEY_EVENT;
        }
        if (!ReadConsoleInputW(E.hin, &record, 1, &count)) {
            if (!blocking) return NO_KEY_EVENT;
            continue;
        }
        if (record.EventType == MOUSE_EVENT) {
            int key = editorReadMouseEvent(&record.Event.MouseEvent);
            if (key) return key;
            continue;
        }
        if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            editorHandleResize();
            return WINDOW_RESIZED;
        }
        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
            continue;

        WORD virtual_key = record.Event.KeyEvent.wVirtualKeyCode;
        DWORD state = record.Event.KeyEvent.dwControlKeyState;
        wchar_t character = record.Event.KeyEvent.uChar.UnicodeChar;
        if (editorCollectPendingPaste(&record)) return PASTE_EVENT;

        int action = editorResolveKeyBinding(virtual_key, state);
        if (action != NO_KEY_EVENT) {
            if (action == ACTION_NEXT_TAB) {
                if (next_tab_shortcut_latched) return NO_KEY_EVENT;
                next_tab_shortcut_latched = 1;
            }
            return action;
        }

        if (!(state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED |
                       LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) &&
            character >= 32 && character < 128)
            return (unsigned char)character;
    }
}

static int editorFinishReadKey(int key) {
    if (key == NO_KEY_EVENT) return key;

    if (key != WINDOW_RESIZED && key != MOUSE_HOVER) {
        E.input_activity_deadline = GetTickCount64() +
            (ULONGLONG)E.ready_timeout_ms;
    }

    return key;
}

int editorReadKey(void) {
    return editorFinishReadKey(editorReadInputEvent(1));
}

int editorReadKeyNonBlocking(void) {
    return editorFinishReadKey(editorReadInputEvent(0));
}
