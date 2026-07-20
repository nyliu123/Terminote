#include <assert.h>
#include <stdio.h>

#include "banner.h"
#include "ascii_banner.h"

static size_t utf8Width(const char *text) {
    size_t width = 0;
    const unsigned char *at = (const unsigned char *)text;
    while (*at) {
        if ((*at & 0xC0) != 0x80) width++;
        at++;
    }
    return width;
}

int main(void) {
    size_t expected_width = utf8Width(bannerLine(0));
    assert(expected_width > 0);
    for (int row = 1; row < BANNER_HEIGHT; row++)
        assert(utf8Width(bannerLine(row)) == expected_width);

    char **compact = asciiBannerTextToRows("AA");
    char **spaced = asciiBannerTextToRows("A A");
    assert(compact && spaced);
    assert(asciiBannerUtf8Width(compact[0]) == 15);
    assert(asciiBannerUtf8Width(spaced[0]) >
           asciiBannerUtf8Width(compact[0]));
    asciiBannerFreeRows(compact);
    asciiBannerFreeRows(spaced);

    char **unsupported = asciiBannerTextToRows("0");
    assert(!unsupported);

    puts("test_banner: PASS");
    return 0;
}
