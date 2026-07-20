#include "common.h"
#include "row.h"
#include "output.h"
#include "file.h"
#include "syntax.h"
#include "paths.h"
#include "localization.h"
#include "brand.h"

#include <commdlg.h>

/* ---- detect and convert UTF-8 ->GBK ---- */
static char *normalizeUtf8(const char *raw, long raw_len, long *out_len) {
    int bom = 0;
    if (raw_len >= 3 && (unsigned char)raw[0] == 0xEF
        && (unsigned char)raw[1] == 0xBB
        && (unsigned char)raw[2] == 0xBF)
        bom = 3;

    /* validate as UTF-8 */
    if (raw_len > bom) {
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                raw + bom, (int)(raw_len - bom), NULL, 0) == 0)
            return NULL;
    }

    if (bom) {
        char *out = malloc(raw_len - bom + 1);
        if (!out) return NULL;
        memcpy(out, raw + bom, raw_len - bom);
        out[raw_len - bom] = '\0';
        *out_len = raw_len - bom;
        return out;
    }
    return NULL;
}

void editorOpen(const char *filename) {
    char *absolute = editorAbsolutePath(filename);
    if (!absolute) die(L("file.absolute_path_failed"));

    free(E.filename);
    E.filename = absolute;
    E.save_failed = 0;
    editorSelectLanguageForFile(E.filename);

    FILE *fp = fopen(E.filename, "rb");
    if (!fp) return;

    /* read entire file into memory */
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *raw = malloc(fsize + 1);
    if (!raw) { fclose(fp); return; }
    if (fsize > 0) fread(raw, 1, fsize, fp);
    raw[fsize] = '\0';
    fclose(fp);

    /* detect and convert UTF-8 ->GBK */
    long   data_len = fsize;
    char  *data     = raw;
    char  *gbk      = NULL;
    if (fsize > 0) {
        gbk = normalizeUtf8(raw, fsize, &data_len);
        if (gbk) data = gbk;
    }

    /* parse into rows */
    char   *line    = NULL;
    size_t  linecap = 0;
    size_t  linelen = 0;

    for (long i = 0; i < data_len; i++) {
        char c = data[i];
        if (linelen + 1 >= linecap) {
            linecap = linecap ? linecap * 2 : 128;
            line = realloc(line, linecap);
            if (!line) die("realloc");
        }
        if (c == '\n') {
            if (linelen > 0 && line[linelen - 1] == '\r') linelen--;
            editorInsertRow(E.numrows, line, linelen);
            linelen = 0;
        } else {
            line[linelen++] = c;
        }
    }

    if (linelen > 0) {
        if (linelen > 0 && line[linelen - 1] == '\r') linelen--;
        editorInsertRow(E.numrows, line, linelen);
    }

    free(line);
    free(raw);
    free(gbk);
    E.dirty = 0;
    editorUpdateLineNumWidth();
}

static char *editorChooseSavePath(void) {
    char selected[32768] = "";
    char title[MAX_PATH + 32];
    if (E.filename) {
        size_t length = strlen(E.filename);
        if (length >= sizeof(selected)) {
            editorSetStatusMessage("%s", L("file.save_path_long"));
            return NULL;
        }
        memcpy(selected, E.filename, length + 1);
    }

    OPENFILENAMEA dialog;
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetConsoleWindow();
    dialog.lpstrFilter = "Any file (*.*)\0*.*\0\0";
    dialog.lpstrFile = selected;
    dialog.nMaxFile = sizeof(selected);
    snprintf(title, sizeof(title), "%s - Save file", brandTitleName());
    dialog.lpstrTitle = title;
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                   OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameA(&dialog)) {
        DWORD error = CommDlgExtendedError();
        if (error != 0)
            editorSetStatusMessage(L("file.save_dialog_failed"), error);
        else
            editorSetStatusMessage("%s", L("file.save_canceled"));
        return NULL;
    }

    char *absolute = editorAbsolutePath(selected);
    if (!absolute)
        editorSetStatusMessage("%s", L("file.save_path_failed"));
    return absolute;
}

char *editorChooseOpenPath(void) {
    char selected[32768] = "";
    char title[MAX_PATH + 32];
    OPENFILENAMEA dialog;
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetConsoleWindow();
    dialog.lpstrFilter = "Any file (*.*)\0*.*\0\0";
    dialog.lpstrFile = selected;
    dialog.nMaxFile = sizeof(selected);
    snprintf(title, sizeof(title), "%s - Open file", brandTitleName());
    dialog.lpstrTitle = title;
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST |
                   OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&dialog)) {
        DWORD error = CommDlgExtendedError();
        if (error != 0)
            editorSetStatusMessage(L("file.open_dialog_failed"), error);
        return NULL;
    }
    char *absolute = editorAbsolutePath(selected);
    if (!absolute) editorSetStatusMessage("%s", L("file.open_path_failed"));
    return absolute;
}

int editorSaveAs(void) {
    char *chosen_path = editorChooseSavePath();
    if (!chosen_path) return 0;

    int len;
    char *buf = editorRowsToString(&len);
    FILE *fp = fopen(chosen_path, "wb");
    if (!fp) {
        free(buf);
        E.save_failed = 1;
        editorSetStatusMessage(L("file.save_failed"), chosen_path);
        free(chosen_path);
        return 0;
    }
    size_t written = fwrite(buf, 1, len, fp);
    int close_result = fclose(fp);
    free(buf);
    if ((int)written != len || close_result != 0) {
        E.save_failed = 1;
        editorSetStatusMessage(L("file.save_failed"), chosen_path);
        free(chosen_path);
        return 0;
    }
    free(E.filename);
    E.filename = chosen_path;
    E.save_failed = 0;
    E.dirty = 0;
    editorSelectLanguageForFile(E.filename);
    editorRefreshAllSyntax();
    editorSetStatusMessage(L("file.saved_as"), E.filename);
    return 1;
}

int editorSave(void) {
    char *chosen_path = NULL;
    const char *target_path = E.filename;
    if (E.filename == NULL || E.save_failed) {
        chosen_path = editorChooseSavePath();
        if (!chosen_path) return 0;
        target_path = chosen_path;
    }

    int  len;
    char *buf = editorRowsToString(&len);
    FILE *fp  = fopen(target_path, "wb");

    if (!fp) {
        free(buf);
        E.save_failed = 1;
        editorSetStatusMessage(L("file.save_failed"), target_path);
        free(chosen_path);
        return 0;
    }

    size_t written = fwrite(buf, 1, len, fp);
    int close_result = fclose(fp);
    free(buf);

    if ((int)written != len || close_result != 0) {
        E.save_failed = 1;
        editorSetStatusMessage(L("file.save_failed"), target_path);
        free(chosen_path);
        return 0;
    }

    if (chosen_path) {
        free(E.filename);
        E.filename = chosen_path;
    }
    E.save_failed = 0;
    E.dirty = 0;
    editorSelectLanguageForFile(E.filename);
    editorRefreshAllSyntax();
    editorSetStatusMessage(L("file.saved_bytes"), len);
    return 1;
}
