#ifndef CLAUDE_USER_LIBC_UNISTD_H
#define CLAUDE_USER_LIBC_UNISTD_H

#include <stdint.h>
#include <stddef.h>

typedef int32_t ssize_t;

#define O_READ   0x1U
#define O_WRITE  0x2U

struct kbd_event {
    uint8_t scancode;
    uint8_t pressed;
    uint8_t extended;
    uint8_t reserved;
};

ssize_t write(int fd, const void *buf, uint32_t len);
int open(const char *path, uint32_t flags);
ssize_t read(int fd, void *buf, uint32_t len);
int close(int fd);
int fork(void);
int exec(const char *path);
int getpid(void);
int proc_count(void);
int lseek(int fd, int32_t offset, int whence);
int kbd_read_event(struct kbd_event *event);
int fb_present(const void *pixels, uint32_t width, uint32_t height);
uint32_t ticks_ms(void);
void *sbrk(int32_t increment);
void exit(int status) __attribute__((noreturn));

#endif /* CLAUDE_USER_LIBC_UNISTD_H */
