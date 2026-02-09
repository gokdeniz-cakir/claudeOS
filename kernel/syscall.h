#ifndef CLAUDE_SYSCALL_H
#define CLAUDE_SYSCALL_H

#include "isr.h"

/* Initialize syscall subsystem state (INT 0x80 gate is installed by idt_init). */
void syscall_init(void);

/* C-level syscall handler invoked by INT 0x80 assembly stub. */
void syscall_handler(struct isr_regs *regs);

#endif /* CLAUDE_SYSCALL_H */
