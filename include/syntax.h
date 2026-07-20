#ifndef SYNTAX_H
#define SYNTAX_H

void editorSelectLanguageForFile(const char *filename);
void editorRefreshAllSyntax(void);
int editorSyntaxEnabled(void);
const char *editorSyntaxLineComment(void);
const char *editorSyntaxBlockCommentStart(void);
const char *editorSyntaxBlockCommentEnd(void);
const char *editorSyntaxSectionStart(void);
const char *editorSyntaxSectionEnd(void);
int editorSyntaxSectionColor(void);
int editorSyntaxMultilineStringCount(void);
const char *editorSyntaxMultilineStringAt(int index);
int editorSyntaxKeywordMatch(const char *text, int *length);
int editorSyntaxKeywordColor(int highlight);
int editorSyntaxCompletionCount(const char *prefix, int prefix_length);
const char *editorSyntaxCompletionAt(const char *prefix, int prefix_length,
                                     int index);

#endif
