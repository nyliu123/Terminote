#ifndef PATHS_H
#define PATHS_H

#define EDITOR_CONFIG_DIR "config"
#define EDITOR_PREFERENCES_FILE EDITOR_CONFIG_DIR "\\preferences.ini"
#define EDITOR_KEY_BINDINGS_FILE EDITOR_CONFIG_DIR "\\key-bindings.ini"
#define EDITOR_EXECUTION_FILE EDITOR_CONFIG_DIR "\\execution.ini"
#define EDITOR_RESOURCES_DIR "resources"
#define EDITOR_LANGUAGES_DIR EDITOR_RESOURCES_DIR "\\languages"
#define EDITOR_LOCALIZATION_DIR EDITOR_RESOURCES_DIR "\\localization"
#define EDITOR_LOGS_DIR "logs"

int editorExecutableDirectory(char *directory, size_t size);
int editorExecutablePath(const char *relative, char *path, size_t size);
char *editorAbsolutePath(const char *path);

#endif
