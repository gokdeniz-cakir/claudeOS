#include "strings.h"

#include "ctype.h"

int strcasecmp(const char *a, const char *b)
{
    size_t i = 0U;

    if (a == b) {
        return 0;
    }

    if (a == 0) {
        return -1;
    }

    if (b == 0) {
        return 1;
    }

    while (a[i] != '\0' && b[i] != '\0') {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) {
            return ca - cb;
        }
        i++;
    }

    return tolower((unsigned char)a[i]) - tolower((unsigned char)b[i]);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    size_t i;

    if (n == 0U) {
        return 0;
    }

    if (a == b) {
        return 0;
    }

    if (a == 0) {
        return -1;
    }

    if (b == 0) {
        return 1;
    }

    for (i = 0U; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb || a[i] == '\0' || b[i] == '\0') {
            return ca - cb;
        }
    }

    return 0;
}
