#include <stdlib.h>
#include <string.h>

#include "ascii_banner.h"
#include "brand.h"
#include "banner.h"

static char **banner_rows;

static void padBannerRows(void) {
    size_t maximum_width = 0;
    for (int row = 0; row < BANNER_HEIGHT; row++) {
        size_t width = asciiBannerUtf8Width(banner_rows[row]);
        if (width > maximum_width) maximum_width = width;
    }

    for (int row = 0; row < BANNER_HEIGHT; row++) {
        size_t width = asciiBannerUtf8Width(banner_rows[row]);
        size_t padding = maximum_width - width;
        if (padding == 0) continue;

        size_t length = strlen(banner_rows[row]);
        char *padded = realloc(banner_rows[row], length + padding + 1);
        if (!padded) continue;
        memset(padded + length, ' ', padding);
        padded[length + padding] = '\0';
        banner_rows[row] = padded;
    }
}

static void initializeBanner(void) {
    if (banner_rows) return;
    banner_rows = asciiBannerTextToRows(brandUpperName());
    if (banner_rows) padBannerRows();
}

const char *bannerLine(int row) {
    initializeBanner();
    if (row < 0 || row >= BANNER_HEIGHT || !banner_rows) return "";
    return banner_rows[row];
}
