#ifndef CLAUDE_USER_LIBC_STRING_H
#define CLAUDE_USER_LIBC_STRING_H

#include <stddef.h>
#include <stdint.h>

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);

#endif /* CLAUDE_USER_LIBC_STRING_H */
