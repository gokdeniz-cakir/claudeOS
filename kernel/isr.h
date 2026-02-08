#ifndef CLAUDE_ISR_H
#define CLAUDE_ISR_H

#include <stdint.h>

/* Register state pushed by the ISR common stub.
 * Layout must match the pushad + segment register push order in isr_stubs.asm.
 * The struct is passed by pointer (pushed esp) to the C handler. */
struct isr_regs {
    /* Segment registers (pushed manually) */
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    /* Pushed by pushad: EDI, ESI, EBP, ESP(orig), EBX, EDX, ECX, EAX */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;       /* Value from pushad (not useful) */
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    /* Pushed by the ISR stub */
    uint32_t int_no;
    uint32_t err_code;

    /* Pushed by the CPU automatically */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;

    /* Only pushed if privilege level change occurred */
    /* uint32_t user_esp; */
    /* uint32_t user_ss;  */
};

/* C-level ISR handler called from the assembly common stub */
void isr_handler(struct isr_regs *regs);

#endif /* CLAUDE_ISR_H */
