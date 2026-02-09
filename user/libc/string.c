#include "string.h"

size_t strlen(const char *s)
{
    size_t len = 0U;

    if (s == 0) {
        return 0U;
    }

    while (s[len] != '\0') {
        len++;
    }

    return len;
}

int strcmp(const char *a, const char *b)
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
        if (a[i] != b[i]) {
            return ((unsigned char)a[i]) - ((unsigned char)b[i]);
        }
        i++;
    }

    return ((unsigned char)a[i]) - ((unsigned char)b[i]);
}

int strncmp(const char *a, const char *b, size_t n)
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
        if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0') {
            return ((unsigned char)a[i]) - ((unsigned char)b[i]);
        }
    }

    return 0;
}

char *strcpy(char *dst, const char *src)
{
    size_t i = 0U;

    if (dst == 0 || src == 0) {
        return dst;
    }

    while (src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    size_t i;
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == 0 || s == 0 || n == 0U) {
        return dst;
    }

    for (i = 0U; i < n; i++) {
        d[i] = s[i];
    }

    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    size_t i;
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || n == 0U) {
        return dst;
    }

    if (d < s) {
        for (i = 0U; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (i = n; i > 0U; i--) {
            d[i - 1U] = s[i - 1U];
        }
    }

    return dst;
}

void *memset(void *dst, int value, size_t n)
{
    size_t i;
    uint8_t *d = (uint8_t *)dst;

    if (d == 0 || n == 0U) {
        return dst;
    }

    for (i = 0U; i < n; i++) {
        d[i] = (uint8_t)value;
    }

    return dst;
}
