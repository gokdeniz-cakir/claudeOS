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

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0U;

    if (dst == 0 || src == 0 || n == 0U) {
        return dst;
    }

    while (i < n && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    while (i < n) {
        dst[i] = '\0';
        i++;
    }

    return dst;
}

char *strcat(char *dst, const char *src)
{
    size_t dst_len;
    size_t i = 0U;

    if (dst == 0 || src == 0) {
        return dst;
    }

    dst_len = strlen(dst);
    while (src[i] != '\0') {
        dst[dst_len + i] = src[i];
        i++;
    }
    dst[dst_len + i] = '\0';
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    size_t dst_len;
    size_t i = 0U;

    if (dst == 0 || src == 0) {
        return dst;
    }

    dst_len = strlen(dst);
    while (i < n && src[i] != '\0') {
        dst[dst_len + i] = src[i];
        i++;
    }
    dst[dst_len + i] = '\0';
    return dst;
}

char *strchr(const char *s, int c)
{
    size_t i = 0U;
    char ch;

    if (s == 0) {
        return 0;
    }

    ch = (char)c;
    while (s[i] != '\0') {
        if (s[i] == ch) {
            return (char *)&s[i];
        }
        i++;
    }

    if (ch == '\0') {
        return (char *)&s[i];
    }

    return 0;
}

char *strrchr(const char *s, int c)
{
    size_t i = 0U;
    char ch;
    char *last = 0;

    if (s == 0) {
        return 0;
    }

    ch = (char)c;
    while (s[i] != '\0') {
        if (s[i] == ch) {
            last = (char *)&s[i];
        }
        i++;
    }

    if (ch == '\0') {
        return (char *)&s[i];
    }

    return last;
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

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *lhs = (const uint8_t *)a;
    const uint8_t *rhs = (const uint8_t *)b;
    size_t i;

    if (lhs == rhs || n == 0U) {
        return 0;
    }

    if (lhs == 0) {
        return -1;
    }

    if (rhs == 0) {
        return 1;
    }

    for (i = 0U; i < n; i++) {
        if (lhs[i] != rhs[i]) {
            return ((int)lhs[i]) - ((int)rhs[i]);
        }
    }

    return 0;
}
