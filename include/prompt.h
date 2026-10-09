#ifndef PROMPT_H
#define PROMPT_H

char *editorPrompt(char *prompt, void (*callback)(char *, int));
char *editorSearchPrompt(char *prompt, void (*callback)(char *, int));
char *editorSearchSelectionPrompt(char *prompt, void (*callback)(char *, int));

#endif
