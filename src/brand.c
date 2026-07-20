#include "common.h"
#include "brand.h"

static int brand_initialized;
static char lower_name[MAX_PATH] = "editor";
static char title_name[MAX_PATH] = "Editor";
static char upper_name[MAX_PATH] = "EDITOR";

static void copyWithCase(char *destination, size_t capacity,
                         const char *source, int upper) {
    size_t index = 0;
    while (source[index] && index + 1 < capacity) {
        unsigned char character = (unsigned char)source[index];
        destination[index] = upper
            ? (char)toupper(character) : (char)tolower(character);
        index++;
    }
    destination[index] = '\0';
}

void brandInitialize(void) {
    if (brand_initialized) return;
    brand_initialized = 1;

    char executable[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, executable, sizeof(executable));
    if (length == 0 || length >= sizeof(executable)) return;

    const char *base = executable;
    for (const char *at = executable; *at; at++)
        if (*at == '\\' || *at == '/') base = at + 1;

    char name[MAX_PATH];
    snprintf(name, sizeof(name), "%s", base);
    char *extension = strrchr(name, '.');
    if (extension && _stricmp(extension, ".exe") == 0) *extension = '\0';
    if (!name[0]) return;

    copyWithCase(lower_name, sizeof(lower_name), name, 0);
    copyWithCase(upper_name, sizeof(upper_name), name, 1);
    snprintf(title_name, sizeof(title_name), "%s", lower_name);
    title_name[0] = (char)toupper((unsigned char)title_name[0]);
}

const char *brandLowerName(void) {
    brandInitialize();
    return lower_name;
}

const char *brandTitleName(void) {
    brandInitialize();
    return title_name;
}

const char *brandUpperName(void) {
    brandInitialize();
    return upper_name;
}

char *brandExpandText(const char *text) {
    static const char *tokens[] = {"{name}", "{Name}", "{NAME}"};
    const char *values[] = {
        brandLowerName(), brandTitleName(), brandUpperName()
    };
    size_t capacity = strlen(text) + 1;
    for (int token = 0; token < 3; token++) {
        const char *at = text;
        size_t token_length = strlen(tokens[token]);
        size_t value_length = strlen(values[token]);
        while ((at = strstr(at, tokens[token])) != NULL) {
            if (value_length > token_length)
                capacity += value_length - token_length;
            at += token_length;
        }
    }

    char *expanded = malloc(capacity);
    if (!expanded) return NULL;
    char *write = expanded;
    while (*text) {
        int matched = 0;
        for (int token = 0; token < 3; token++) {
            size_t length = strlen(tokens[token]);
            if (strncmp(text, tokens[token], length) != 0) continue;
            size_t value_length = strlen(values[token]);
            memcpy(write, values[token], value_length);
            write += value_length;
            text += length;
            matched = 1;
            break;
        }
        if (!matched) *write++ = *text++;
    }
    *write = '\0';
    return expanded;
}
