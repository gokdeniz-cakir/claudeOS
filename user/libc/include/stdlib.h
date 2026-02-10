#ifndef CLAUDE_USER_LIBC_STDLIB_H
#define CLAUDE_USER_LIBC_STDLIB_H

#include <stddef.h>
#include <stdint.h>

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void exit(int status);

int atoi(const char *str);
long atol(const char *str);
double atof(const char *str);

int abs(int value);
long labs(long value);

char *getenv(const char *name);
int system(const char *command);

#endif /* CLAUDE_USER_LIBC_STDLIB_H */
