#ifndef CLAUDE_IRQ_H
#define CLAUDE_IRQ_H

#include <stdint.h>
#include "isr.h"

#define IRQ_COUNT       16      /* 16 hardware IRQ lines (0-15) */
#define IRQ_BASE        0x20    /* First IRQ maps to INT 32 */

/* Function pointer type for IRQ handlers */
typedef void (*irq_handler_t)(struct isr_regs *regs);

/* Register a handler for a hardware IRQ (0-15) */
void irq_register_handler(uint8_t irq, irq_handler_t handler);

/* Called from irq_stubs.asm common stub — dispatches to registered handler */
void irq_handler(struct isr_regs *regs);

/* Initialize IRQ handling: install IDT gates for IRQs 0-15 */
void irq_init(void);

#endif /* CLAUDE_IRQ_H */
