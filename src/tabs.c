#include "common.h"
#include "tabs.h"
#include "row.h"
#include "output.h"
#include "colors.h"
#include "syntax.h"
#include "file.h"
#include "paths.h"
#include "localization.h"

struct editorDocument {
    int cx, cy, rx, rowoff, coloff;
    int numrows;
    erow *row;
    int dirty;
    char *filename;
    int save_failed, welcome_dismissed, linenum_width;
    int sel_anchor_row, sel_anchor_col, sel_active;
    int mouse_selecting, wheel_scrolling;
    int hscroll_dragging, hscroll_start_coloff, hscroll_start_mx;
    int auto_scroll_h, auto_scroll_v, mouse_drag_move;
    int drag_sel_sr, drag_sel_sc, drag_sel_er, drag_sel_ec;
    char *drag_text;
    int drag_text_len;
    struct undoRecord *undo_stack, *redo_stack;
    int undo_count, undo_cap, redo_count, redo_cap;
};

static struct editorDocument *tabs = NULL;
static int tab_count = 0;
static int tab_capacity = 0;
static int current_tab = 0;
static int dropdown_open = 0;
static int dropdown_first = 0;
static int hovered_tab = -1;
static int suppress_mouse_release = 0;

static void formatTabName(const struct editorDocument *doc,
                          char *buffer, size_t size);

static void saveDocument(struct editorDocument *doc) {
    doc->cx = E.cx; doc->cy = E.cy; doc->rx = E.rx;
    doc->rowoff = E.rowoff; doc->coloff = E.coloff;
    doc->numrows = E.numrows; doc->row = E.row;
    doc->dirty = E.dirty; doc->filename = E.filename;
    doc->save_failed = E.save_failed;
    doc->welcome_dismissed = E.welcome_dismissed;
    doc->linenum_width = E.linenum_width;
    doc->sel_anchor_row = E.sel_anchor_row;
    doc->sel_anchor_col = E.sel_anchor_col;
    doc->sel_active = E.sel_active;
    doc->mouse_selecting = E.mouse_selecting;
    doc->wheel_scrolling = E.wheel_scrolling;
    doc->hscroll_dragging = E.hscroll_dragging;
    doc->hscroll_start_coloff = E.hscroll_start_coloff;
    doc->hscroll_start_mx = E.hscroll_start_mx;
    doc->auto_scroll_h = E.auto_scroll_h;
    doc->auto_scroll_v = E.auto_scroll_v;
    doc->mouse_drag_move = E.mouse_drag_move;
    doc->drag_sel_sr = E.drag_sel_sr; doc->drag_sel_sc = E.drag_sel_sc;
    doc->drag_sel_er = E.drag_sel_er; doc->drag_sel_ec = E.drag_sel_ec;
    doc->drag_text = E.drag_text; doc->drag_text_len = E.drag_text_len;
    doc->undo_stack = undo_stack; doc->undo_count = undo_count;
    doc->undo_cap = undo_cap;
    doc->redo_stack = redo_stack; doc->redo_count = redo_count;
    doc->redo_cap = redo_cap;
}

static void loadDocument(const struct editorDocument *doc) {
    E.cx = doc->cx; E.cy = doc->cy; E.rx = doc->rx;
    E.rowoff = doc->rowoff; E.coloff = doc->coloff;
    E.numrows = doc->numrows; E.row = doc->row;
    E.dirty = doc->dirty; E.filename = doc->filename;
    E.save_failed = doc->save_failed;
    E.welcome_dismissed = doc->welcome_dismissed;
    E.linenum_width = doc->linenum_width ? doc->linenum_width : 3;
    E.sel_anchor_row = doc->sel_anchor_row;
    E.sel_anchor_col = doc->sel_anchor_col;
    E.sel_active = doc->sel_active;
    E.mouse_selecting = doc->mouse_selecting;
    E.wheel_scrolling = doc->wheel_scrolling;
    E.hscroll_dragging = doc->hscroll_dragging;
    E.hscroll_start_coloff = doc->hscroll_start_coloff;
    E.hscroll_start_mx = doc->hscroll_start_mx;
    E.auto_scroll_h = doc->auto_scroll_h;
    E.auto_scroll_v = doc->auto_scroll_v;
    E.mouse_drag_move = doc->mouse_drag_move;
    E.drag_sel_sr = doc->drag_sel_sr; E.drag_sel_sc = doc->drag_sel_sc;
    E.drag_sel_er = doc->drag_sel_er; E.drag_sel_ec = doc->drag_sel_ec;
    E.drag_text = doc->drag_text; E.drag_text_len = doc->drag_text_len;
    undo_stack = doc->undo_stack; undo_count = doc->undo_count;
    undo_cap = doc->undo_cap;
    redo_stack = doc->redo_stack; redo_count = doc->redo_count;
    redo_cap = doc->redo_cap;
    E.statusmsg[0] = '\0';
    E.cur_event[0] = '\0';
    editorSelectLanguageForFile(E.filename);
    editorRefreshAllSyntax();
}

static void ensureCapacity(void) {
    if (tab_count < tab_capacity) return;
    tab_capacity = tab_capacity ? tab_capacity * 2 : 4;
    tabs = realloc(tabs, tab_capacity * sizeof(*tabs));
    if (!tabs) die("realloc: tabs");
}

void editorTabsInitialize(void) {
    ensureCapacity();
    memset(&tabs[0], 0, sizeof(tabs[0]));
    tab_count = 1;
    current_tab = 0;
    saveDocument(&tabs[0]);
}

void editorTabsSyncCurrent(void) {
    if (tab_count > 0) saveDocument(&tabs[current_tab]);
}

static void switchTo(int index) {
    if (index < 0 || index >= tab_count) return;
    if (index != current_tab) {
        editorTabsSyncCurrent();
        current_tab = index;
        loadDocument(&tabs[current_tab]);
    } else {
        editorTabsSyncCurrent();
    }
    if (tab_count <= 1) return;
    char display_name[1024];
    if (E.filename)
        snprintf(display_name, sizeof(display_name), "%s", E.filename);
    else
        formatTabName(&tabs[current_tab], display_name, sizeof(display_name));
    editorSetEvent(L("tabs.switched"), display_name);
}

int editorTabsOpenPath(const char *path) {
    dropdown_open = 0;
    char *absolute = editorAbsolutePath(path);
    if (!absolute) {
        editorSetStatusMessage("%s", L("file.open_path_failed"));
        return 0;
    }
    for (int i = 0; i < tab_count; i++) {
        if (tabs[i].filename && _stricmp(tabs[i].filename, absolute) == 0) {
            switchTo(i);
            free(absolute);
            return 1;
        }
    }
    editorTabsSyncCurrent();
    ensureCapacity();
    memset(&tabs[tab_count], 0, sizeof(tabs[tab_count]));
    current_tab = tab_count++;
    loadDocument(&tabs[current_tab]);
    editorOpen(absolute);
    free(absolute);
    editorTabsSyncCurrent();
    editorSetEvent(L("tabs.opened"), E.filename);
    return 1;
}

void editorTabsNewUntitled(void) {
    dropdown_open = 0;
    editorTabsSyncCurrent();
    ensureCapacity();
    memset(&tabs[tab_count], 0, sizeof(tabs[tab_count]));
    current_tab = tab_count++;
    loadDocument(&tabs[current_tab]);
    editorTabsSyncCurrent();
    editorSetEvent("%s", L("tabs.new_untitled"));
}

void editorNextTab(void) {
    dropdown_open = 0;
    hovered_tab = -1;
    if (tab_count > 1) switchTo((current_tab + 1) % tab_count);
}

static void freeRecords(struct undoRecord *records, int count) {
    for (int i = 0; i < count; i++) {
        free(records[i].block_text);
        free(records[i].new_text);
    }
    free(records);
}

static void freeDocument(struct editorDocument *doc) {
    for (int i = 0; i < doc->numrows; i++) editorFreeRow(&doc->row[i]);
    free(doc->row);
    free(doc->filename);
    free(doc->drag_text);
    freeRecords(doc->undo_stack, doc->undo_count);
    freeRecords(doc->redo_stack, doc->redo_count);
}

int editorCloseCurrentTab(void) {
    dropdown_open = 0;
    if (tab_count <= 1) return 0;
    editorTabsSyncCurrent();
    freeDocument(&tabs[current_tab]);
    memmove(&tabs[current_tab], &tabs[current_tab + 1],
            (tab_count - current_tab - 1) * sizeof(*tabs));
    tab_count--;
    if (current_tab >= tab_count) current_tab = tab_count - 1;
    loadDocument(&tabs[current_tab]);
    editorSetEvent("%s", L("tabs.closed"));
    return 1;
}

static const char *fileBaseName(const char *filename) {
    if (!filename) return NULL;
    const char *slash = strrchr(filename, '\\');
    const char *forward = strrchr(filename, '/');
    if (forward && (!slash || forward > slash)) slash = forward;
    return slash ? slash + 1 : filename;
}

static void formatTabName(const struct editorDocument *doc,
                          char *buffer, size_t size) {
    if (!doc->filename) {
        int unnamed_count = 0, ordinal = 0;
        int index = (int)(doc - tabs);
        for (int i = 0; i < tab_count; i++) {
            if (!tabs[i].filename) {
                unnamed_count++;
                if (i <= index) ordinal++;
            }
        }
        if (unnamed_count <= 1)
            snprintf(buffer, size, "%s", L("tabs.untitled"));
        else
            snprintf(buffer, size, L("tabs.untitled_numbered"), ordinal);
        return;
    }
    snprintf(buffer, size, "%s", fileBaseName(doc->filename));
}

static void restartSnapshotName(const struct editorDocument *doc, int index,
                                char *buffer, size_t size) {
    const char *source = doc->filename ? fileBaseName(doc->filename) : NULL;
    char name[128];
    int at = 0;
    if (source) {
        while (*source && at < (int)sizeof(name) - 1) {
            unsigned char value = (unsigned char)*source;
            int length = value < 0x80 ? 1 : utf8_seq_len(value);
            if (at + length >= (int)sizeof(name)) break;
            if (length == 1 && (value < 32 || strchr("<>:\"/\\|?*", value))) {
                name[at++] = '_';
                source++;
                continue;
            }
            memcpy(name + at, source, length);
            at += length;
            source += length;
        }
    }
    if (at == 0)
        at = snprintf(name, sizeof(name), "untitled.txt");
    name[at] = '\0';
    snprintf(buffer, size, "tab-%03d-%s", index + 1, name);
}

static int writeRestartSnapshot(const struct editorDocument *doc,
                                const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    int ok = 1;
    for (int row = 0; row < doc->numrows; row++) {
        if (fwrite(doc->row[row].chars, 1, doc->row[row].size, file) !=
                (size_t)doc->row[row].size ||
            fwrite("\r\n", 1, 2, file) != 2) {
            ok = 0;
            break;
        }
    }
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static const char *restartDocumentState(const struct editorDocument *doc) {
    if (!doc->filename ||
        GetFileAttributesA(doc->filename) == INVALID_FILE_ATTRIBUTES)
        return "new";
    return doc->dirty ? "modified" : "saved";
}

int editorTabsWriteRestartSession(const char *directory,
                                  const char *manifest_path) {
    editorTabsSyncCurrent();
    FILE *manifest = fopen(manifest_path, "wb");
    if (!manifest) return 0;
    int ok = fprintf(manifest,
                     "version=2\ncurrent=%d\n"
                     "columns=index\tstate\tdirty\toriginal\tsnapshot\n",
                     current_tab) > 0;
    for (int index = 0; ok && index < tab_count; index++) {
        char name[160];
        char path[MAX_PATH];
        restartSnapshotName(&tabs[index], index, name, sizeof(name));
        int length = snprintf(path, sizeof(path), "%s\\%s", directory, name);
        if (length < 0 || length >= (int)sizeof(path) ||
            !writeRestartSnapshot(&tabs[index], path) ||
            fprintf(manifest, "%d\t%s\t%d\t%s\t%s\n", index,
                    restartDocumentState(&tabs[index]), tabs[index].dirty,
                    tabs[index].filename ? tabs[index].filename : "", path) <= 0)
            ok = 0;
    }
    if (fclose(manifest) != 0) ok = 0;
    return ok;
}

static int parseRestartTab(char *line, int *index, const char **state,
                           int *dirty, const char **original,
                           const char **snapshot) {
    char *fields[5];
    fields[0] = line;
    for (int field = 1; field < 5; field++) {
        char *separator = strchr(fields[field - 1], '\t');
        if (!separator) return 0;
        *separator = '\0';
        fields[field] = separator + 1;
    }
    if (strchr(fields[4], '\t')) return 0;

    char *end;
    long parsed_index = strtol(fields[0], &end, 10);
    if (*end || parsed_index < 0 || parsed_index > INT_MAX) return 0;
    long parsed_dirty = strtol(fields[2], &end, 10);
    if (*end || parsed_dirty < 0 || parsed_dirty > INT_MAX) return 0;
    if (strcmp(fields[1], "new") != 0 &&
        strcmp(fields[1], "saved") != 0 &&
        strcmp(fields[1], "modified") != 0)
        return 0;
    if (!fields[4][0]) return 0;

    *index = (int)parsed_index;
    *state = fields[1];
    *dirty = (int)parsed_dirty;
    *original = fields[3];
    *snapshot = fields[4];
    return 1;
}

static int restoreRestartTab(const char *state, int dirty,
                             const char *original, const char *snapshot,
                             int first) {
    if ((strcmp(state, "saved") == 0 && (!original[0] || dirty != 0)) ||
        (strcmp(state, "modified") == 0 && (!original[0] || dirty == 0)))
        return 0;
    DWORD attributes = GetFileAttributesA(snapshot);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    if (first) {
        editorOpen(snapshot);
        editorTabsSyncCurrent();
    } else if (!editorTabsOpenPath(snapshot)) {
        return 0;
    }

    /* E.filename drives both the displayed path and editorSave() target. */
    free(E.filename);
    E.filename = original[0] ? editor_strdup(original) : NULL;
    E.dirty = dirty;
    E.save_failed = 0;
    editorSelectLanguageForFile(E.filename);
    editorRefreshAllSyntax();
    editorTabsSyncCurrent();
    return 1;
}

int editorTabsRestoreRestartSession(const char *manifest_path) {
    FILE *manifest = fopen(manifest_path, "rb");
    if (!manifest) return 0;
    char line[MAX_PATH * 2];
    if (!fgets(line, sizeof(line), manifest)) {
        fclose(manifest);
        return 0;
    }
    if (strcmp(line, "version=2\r\n") != 0 &&
        strcmp(line, "version=2\n") != 0) {
        fclose(manifest);
        return 0;
    }
    if (!fgets(line, sizeof(line), manifest)) {
        fclose(manifest);
        return 0;
    }
    int active_index;
    if (sscanf(line, "current=%d", &active_index) != 1 || active_index < 0) {
        fclose(manifest);
        return 0;
    }
    if (!fgets(line, sizeof(line), manifest) ||
        strncmp(line, "columns=", 8) != 0) {
        fclose(manifest);
        return 0;
    }

    int expected_index = 0;
    while (fgets(line, sizeof(line), manifest)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        int index, dirty;
        const char *state, *original, *snapshot;
        if (!parseRestartTab(line, &index, &state, &dirty,
                             &original, &snapshot) ||
            index != expected_index ||
            !restoreRestartTab(state, dirty, original, snapshot,
                               expected_index == 0)) {
            fclose(manifest);
            return 0;
        }
        expected_index++;
    }
    fclose(manifest);
    if (expected_index == 0 || active_index >= expected_index) return 0;
    switchTo(active_index);
    editorTabsSyncCurrent();
    return 1;
}

static int dropdownItemCount(void) {
    return tab_count > 0 ? tab_count - 1 : 0;
}

static int dropdownTabAtPosition(int position) {
    if (position < 0) return -1;
    int seen = 0;
    for (int i = 0; i < tab_count; i++) {
        if (i == current_tab) continue;
        if (seen++ == position) return i;
    }
    return -1;
}

static void formatTabPath(const struct editorDocument *doc,
                          char *buffer, size_t size) {
    char name[32768];
    if (doc->filename) snprintf(name, sizeof(name), "%s", doc->filename);
    else formatTabName(doc, name, sizeof(name));
    snprintf(buffer, size, "%s%s", name,
             doc->dirty ? L("tabs.modified_suffix") : "");
}

static void formatTabType(const struct editorDocument *doc,
                          char *buffer, size_t size) {
    if (!doc->filename) {
        snprintf(buffer, size, "%s", L("tabs.file_type"));
        return;
    }
    const char *base = fileBaseName(doc->filename);
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base || !dot[1]) {
        snprintf(buffer, size, "%s", L("tabs.file_type"));
        return;
    }
    char extension[64];
    int at = 0;
    for (dot++; *dot && at < (int)sizeof(extension) - 1; dot++)
        extension[at++] = (char)toupper((unsigned char)*dot);
    extension[at] = '\0';
    snprintf(buffer, size, L("tabs.extension_type"), extension);
}

static int textWidth(const char *text) {
    return utf8_terminal_width(text);
}

const char *editorCurrentTabDisplayName(void) {
    static char name[1024];
    if (tab_count <= 0) return L("tabs.untitled");
    editorTabsSyncCurrent();
    formatTabName(&tabs[current_tab], name, sizeof(name));
    return name;
}

static void appendTitle(const char *title, int width) {
    int used = 0;
    while (*title && used < width) {
        int length;
        int columns = utf8_terminal_char_width(title, &length);
        if (used + columns > width) break;
        char character[5];
        memcpy(character, title, length); character[length] = '\0';
        abAppend(character);
        title += length; used += columns;
    }
    while (used++ < width) abAppend(" ");
}

static void appendTabEntry(const struct editorDocument *doc, int width) {
    char path[32768];
    char type[128];
    formatTabPath(doc, path, sizeof(path));
    formatTabType(doc, type, sizeof(type));

    int type_width = textWidth(type);
    int path_width = width - type_width - 2;
    if (path_width < 1) {
        appendTitle(type, width);
        return;
    }
    abAppend(" ");
    appendTitle(path, path_width);
    abAppend(type);
    abAppend(" ");
}

void editorDrawTabBar(void) {
    editorTabsSyncCurrent();
    abAppend(editorColor(COLOR_TAB_BAR));
    appendTabEntry(&tabs[current_tab], E.screencols);
    abAppend(editorColor(COLOR_RESET));
    abAppend("\r\n");
}

void editorDrawTabDropdown(void) {
    if (!dropdown_open) return;
    editorTabsSyncCurrent();
    int available = dropdownItemCount() - dropdown_first;
    if (available < 0) available = 0;
    int visible = available < E.screenrows ? available : E.screenrows;
    for (int i = 0; i < visible; i++) {
        int index = dropdownTabAtPosition(dropdown_first + i);
        if (index < 0) break;
        char position[32];
        snprintf(position, sizeof(position), "\x1b[%d;1H", i + 2);
        abAppend(position);
        if (index == hovered_tab) {
            abAppend(editorColor(COLOR_TAB_DROPDOWN_HOVER_BACKGROUND));
            abAppend(editorColor(COLOR_TAB_DROPDOWN_HOVER_TEXT));
        } else {
            abAppend(editorColor(COLOR_TAB_DROPDOWN_BACKGROUND));
            abAppend(editorColor(COLOR_TAB_DROPDOWN_TEXT));
        }
        appendTabEntry(&tabs[index], E.screencols);
        abAppend(editorColor(COLOR_RESET));
    }
}

int editorHandleTabMouseEvent(int event) {
    if (suppress_mouse_release && event == MOUSE_RELEASE) {
        suppress_mouse_release = 0;
        return 1;
    }
    if (event == MOUSE_PRESS && E.mouse_y == 0) {
        if (!dropdown_open) {
            dropdown_open = 1;
            dropdown_first = 0;
            hovered_tab = -1;
            E.statusmsg[0] = '\0';
            editorSetEvent("%s", L("editor.ready"));
        } else {
            dropdown_open = 0;
            hovered_tab = -1;
            switchTo(current_tab);
        }
        suppress_mouse_release = 1;
        return 1;
    }
    if (dropdown_open && event == WHEEL_UP) {
        if (dropdown_first > 0) dropdown_first--;
        return 1;
    }
    if (dropdown_open && event == WHEEL_DOWN) {
        if (dropdown_first + E.screenrows < dropdownItemCount())
            dropdown_first++;
        return 1;
    }
    if (dropdown_open && event == MOUSE_HOVER) {
        int position = dropdown_first + E.mouse_y - 1;
        hovered_tab = E.mouse_y >= 1 && E.mouse_y <= E.screenrows
            ? dropdownTabAtPosition(position) : -1;
        return 1;
    }
    if (dropdown_open && event == MOUSE_PRESS) {
        int position = dropdown_first + E.mouse_y - 1;
        int index = E.mouse_y >= 1 && E.mouse_y <= E.screenrows
            ? dropdownTabAtPosition(position) : -1;
        if (index >= 0)
            switchTo(index);
        else
            editorSetEvent("%s", L("editor.ready"));
        dropdown_open = 0;
        hovered_tab = -1;
        suppress_mouse_release = 1;
        return 1;
    }
    if (!dropdown_open && event == MOUSE_HOVER) hovered_tab = -1;
    return E.mouse_y == 0;
}

int editorTabDropdownOpen(void) { return dropdown_open; }
void editorCloseTabDropdown(void) {
    dropdown_open = 0;
    hovered_tab = -1;
}
int editorTabCount(void) { return tab_count; }
