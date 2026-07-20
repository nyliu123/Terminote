#ifndef TABS_H
#define TABS_H

void editorTabsInitialize(void);
void editorTabsSyncCurrent(void);
int editorTabsOpenPath(const char *path);
void editorTabsNewUntitled(void);
void editorNextTab(void);
int editorCloseCurrentTab(void);
void editorDrawTabBar(void);
void editorDrawTabDropdown(void);
int editorHandleTabMouseEvent(int event);
int editorTabDropdownOpen(void);
void editorCloseTabDropdown(void);
const char *editorCurrentTabDisplayName(void);
int editorTabCount(void);
int editorTabsWriteRestartSession(const char *directory,
                                  const char *manifest_path);
int editorTabsRestoreRestartSession(const char *manifest_path);

#endif
