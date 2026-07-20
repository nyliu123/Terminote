#include "ini_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *path = "build/tests/test_ini_config.ini";
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("[language]\n"
          "hash = #\n"
          "semicolon = ;\n"
          "command = echo \\\"ok\\\" && pause\n"
          "escaped = one\\\\two\n", file);
    fclose(file);

    struct iniDocument document;
    struct iniError error;
    assert(iniDocumentLoad(path, 256, &document, &error));
    assert(document.count == 5);
    assert(strcmp(document.items[1].value, "#") == 0);
    assert(strcmp(document.items[2].value, ";") == 0);
    assert(strcmp(document.items[3].value, "echo \\\"ok\\\" && pause") == 0);
    assert(strcmp(document.items[4].value, "one\\\\two") == 0);
    iniDocumentFree(&document);
    remove(path);

    puts("test_ini_config: PASS");
    return 0;
}
