#ifndef CLAUDE_USER_LIBC_STDIO_H
#define CLAUDE_USER_LIBC_STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define EOF      (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct claude_file FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list args);

int sprintf(char *dst, const char *fmt, ...);
int snprintf(char *dst, size_t size, const char *fmt, ...);
int vsprintf(char *dst, const char *fmt, va_list args);
int vsnprintf(char *dst, size_t size, const char *fmt, va_list args);

int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list args);

FILE *fopen(const char *path, const char *mode);
size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
int fclose(FILE *stream);
int fflush(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);

int remove(const char *path);
int rename(const char *old_path, const char *new_path);

#endif /* CLAUDE_USER_LIBC_STDIO_H */
