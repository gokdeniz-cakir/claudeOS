#ifndef CLAUDE_USER_LIBC_UNISTD_H
#define CLAUDE_USER_LIBC_UNISTD_H

#include <stdint.h>
#include <stddef.h>

typedef int32_t ssize_t;

#define O_READ   0x1U
#define O_WRITE  0x2U

ssize_t write(int fd, const void *buf, uint32_t len);
int open(const char *path, uint32_t flags);
ssize_t read(int fd, void *buf, uint32_t len);
int close(int fd);
int fork(void);
int exec(const char *path);
int getpid(void);
int proc_count(void);
void *sbrk(int32_t increment);
void exit(int status) __attribute__((noreturn));

#endif /* CLAUDE_USER_LIBC_UNISTD_H */
