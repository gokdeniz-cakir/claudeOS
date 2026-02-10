#include "unistd.h"

#include <stdint.h>

#define SYSCALL_WRITE  1U
#define SYSCALL_EXIT   2U
#define SYSCALL_SBRK   3U
#define SYSCALL_OPEN   4U
#define SYSCALL_READ   5U
#define SYSCALL_CLOSE  6U
#define SYSCALL_FORK   7U
#define SYSCALL_EXEC   8U
#define SYSCALL_GETPID 9U
#define SYSCALL_PCOUNT 10U
#define SYSCALL_KBD_READ 11U
#define SYSCALL_TICKS_MS 12U
#define SYSCALL_LSEEK 13U

static inline uint32_t syscall3(uint32_t number, uint32_t arg0, uint32_t arg1,
                                uint32_t arg2)
{
    uint32_t ret;

    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(number), "b"(arg0), "c"(arg1), "d"(arg2)
                      : "memory", "cc");

    return ret;
}

ssize_t write(int fd, const void *buf, uint32_t len)
{
    return (ssize_t)(int32_t)syscall3(SYSCALL_WRITE, (uint32_t)fd,
                                      (uint32_t)(uintptr_t)buf, len);
}

int open(const char *path, uint32_t flags)
{
    return (int)(int32_t)syscall3(SYSCALL_OPEN, (uint32_t)(uintptr_t)path,
                                  flags, 0U);
}

ssize_t read(int fd, void *buf, uint32_t len)
{
    return (ssize_t)(int32_t)syscall3(SYSCALL_READ, (uint32_t)fd,
                                      (uint32_t)(uintptr_t)buf, len);
}

int close(int fd)
{
    return (int)(int32_t)syscall3(SYSCALL_CLOSE, (uint32_t)fd, 0U, 0U);
}

int fork(void)
{
    return (int)(int32_t)syscall3(SYSCALL_FORK, 0U, 0U, 0U);
}

int exec(const char *path)
{
    return (int)(int32_t)syscall3(SYSCALL_EXEC, (uint32_t)(uintptr_t)path,
                                  0U, 0U);
}

int getpid(void)
{
    return (int)(int32_t)syscall3(SYSCALL_GETPID, 0U, 0U, 0U);
}

int proc_count(void)
{
    return (int)(int32_t)syscall3(SYSCALL_PCOUNT, 0U, 0U, 0U);
}

int lseek(int fd, int32_t offset, int whence)
{
    return (int)(int32_t)syscall3(SYSCALL_LSEEK, (uint32_t)fd,
                                  (uint32_t)offset, (uint32_t)whence);
}

int kbd_read_event(struct kbd_event *event)
{
    return (int)(int32_t)syscall3(SYSCALL_KBD_READ,
                                  (uint32_t)(uintptr_t)event, 0U, 0U);
}

uint32_t ticks_ms(void)
{
    return syscall3(SYSCALL_TICKS_MS, 0U, 0U, 0U);
}

void *sbrk(int32_t increment)
{
    return (void *)(uintptr_t)syscall3(SYSCALL_SBRK, (uint32_t)increment,
                                       0U, 0U);
}

void exit(int status)
{
    (void)syscall3(SYSCALL_EXIT, (uint32_t)status, 0U, 0U);

    for (;;) {
        __asm__ volatile ("pause");
    }
}
