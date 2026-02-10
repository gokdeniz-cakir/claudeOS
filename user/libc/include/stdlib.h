#ifndef CLAUDE_USER_LIBC_STDLIB_H
#define CLAUDE_USER_LIBC_STDLIB_H

#include <stddef.h>
#include <stdint.h>

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

#endif /* CLAUDE_USER_LIBC_STDLIB_H */
