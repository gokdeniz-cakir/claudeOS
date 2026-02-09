#include "syscall.h"

#include <stdint.h>

#include "serial.h"

#define SYSCALL_RET_ENOSYS   0xFFFFFFFFU

static uint8_t syscall_trace_once = 0U;

static uint32_t syscall_dispatch(uint32_t number, uint32_t arg0, uint32_t arg1,
                                 uint32_t arg2, uint32_t arg3, uint32_t arg4,
                                 uint32_t arg5)
{
    (void)arg0;
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;

    switch (number) {
        default:
            return SYSCALL_RET_ENOSYS;
    }
}

void syscall_init(void)
{
    syscall_trace_once = 0U;
    serial_puts("[SYSCALL] INT 0x80 interface initialized\n");
}

void syscall_handler(struct isr_regs *regs)
{
    if (regs == 0) {
        return;
    }

    if (syscall_trace_once == 0U) {
        serial_puts("[SYSCALL] first INT 0x80 received\n");
        syscall_trace_once = 1U;
    }

    regs->eax = syscall_dispatch(regs->eax, regs->ebx, regs->ecx, regs->edx,
                                 regs->esi, regs->edi, regs->ebp);
}
