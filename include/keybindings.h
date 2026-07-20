#ifndef KEYBINDINGS_H
#define KEYBINDINGS_H

void editorLoadKeyBindings(void);
int editorResolveKeyBinding(WORD virtual_key, DWORD control_state);
const char *editorBindingLabel(int action);
int editorBindingCount(void);

#endif
