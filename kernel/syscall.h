#ifndef CLAUDE_SYSCALL_H
#define CLAUDE_SYSCALL_H

#include "isr.h"

#define SYSCALL_WRITE    1U
#define SYSCALL_EXIT     2U
#define SYSCALL_SBRK     3U
#define SYSCALL_OPEN     4U
#define SYSCALL_READ     5U
#define SYSCALL_CLOSE    6U
#define SYSCALL_FORK     7U
#define SYSCALL_EXEC     8U
#define SYSCALL_GETPID   9U
#define SYSCALL_PCOUNT   10U

#define SYSCALL_O_READ   0x1U
#define SYSCALL_O_WRITE  0x2U

/* Initialize syscall subsystem state (INT 0x80 gate is installed by idt_init). */
void syscall_init(void);

/* C-level syscall handler invoked by INT 0x80 assembly stub. */
void syscall_handler(struct isr_regs *regs);

#endif /* CLAUDE_SYSCALL_H */
