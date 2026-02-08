#ifndef CLAUDE_PIC_H
#define CLAUDE_PIC_H

#include <stdint.h>

/* 8259 PIC I/O ports */
#define PIC1_COMMAND    0x20    /* Master PIC command port */
#define PIC1_DATA       0x21    /* Master PIC data port */
#define PIC2_COMMAND    0xA0    /* Slave PIC command port */
#define PIC2_DATA       0xA1    /* Slave PIC data port */

/* PIC commands */
#define PIC_EOI         0x20    /* End-of-interrupt command */

/* ICW1 flags */
#define ICW1_INIT       0x10    /* Initialization required */
#define ICW1_ICW4       0x01    /* ICW4 will be present */

/* ICW4 flags */
#define ICW4_8086       0x01    /* 8086/88 mode */

/* Remapped IRQ vector offsets */
#define PIC1_OFFSET     0x20    /* Master: IRQ 0-7  -> INT 32-39 */
#define PIC2_OFFSET     0x28    /* Slave:  IRQ 8-15 -> INT 40-47 */

/* Initialize both PICs: remap IRQs, mask all interrupts */
void pic_init(void);

/* Send end-of-interrupt for the given IRQ (0-15) */
void pic_send_eoi(uint8_t irq);

/* Return combined 16-bit PIC ISR (bit set = IRQ is in service). */
uint16_t pic_get_isr(void);

/* Check for PIC spurious IRQ7/IRQ15.
 * Returns 1 for spurious, 0 for real/unsupported IRQ.
 * For spurious IRQ15, this helper also sends master EOI as required. */
uint8_t pic_is_spurious_irq(uint8_t irq);

/* Unmask (enable) a specific IRQ line (0-15) */
void pic_clear_mask(uint8_t irq);

#endif /* CLAUDE_PIC_H */
