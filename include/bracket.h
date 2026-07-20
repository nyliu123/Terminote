#ifndef BRACKET_H
#define BRACKET_H

void editorUpdateBracketMatch(void);
void editorSuppressBracketMatchAtCursor(void);
int editorBracketMatchedAt(int row, int render_index);

#endif
