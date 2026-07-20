#ifndef ASCII_BANNER_H
#define ASCII_BANNER_H

#include <stddef.h>

char **asciiBannerTextToRows(const char *text);
void asciiBannerFreeRows(char **rows);
size_t asciiBannerUtf8Width(const char *text);

#endif
