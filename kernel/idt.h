#ifndef CLAUDE_IDT_H
#define CLAUDE_IDT_H

#include <stdint.h>

/* IDT gate entry (8 bytes per entry on IA-32) */
struct idt_entry {
    uint16_t offset_low;    /* Offset bits 0..15 */
    uint16_t selector;      /* Code segment selector in GDT */
    uint8_t  zero;          /* Unused, must be 0 */
    uint8_t  type_attr;     /* Gate type, DPL, and present bit */
    uint16_t offset_high;   /* Offset bits 16..31 */
} __attribute__((packed));

/* IDTR descriptor: loaded by lidt instruction */
struct idt_ptr {
    uint16_t limit;         /* Size of IDT - 1 */
    uint32_t base;          /* Linear address of the IDT */
} __attribute__((packed));

#define KERNEL_CS           0x08    /* GDT code segment selector */
#define IDT_ENTRIES         256
#define IDT_GATE_INT32      0x8E    /* Present, DPL=0, 32-bit interrupt gate */
#define IDT_GATE_TRAP32     0x8F    /* Present, DPL=0, 32-bit trap gate */
#define IDT_GATE_INT32_USER 0xEE    /* Present, DPL=3, 32-bit interrupt gate */
#define IDT_GATE_TRAP32_USER 0xEF   /* Present, DPL=3, 32-bit trap gate */

/* Set a single IDT gate entry */
void idt_set_gate(uint8_t num, uint32_t offset, uint16_t selector,
                  uint8_t type_attr);

/* Initialize the IDT with exception ISRs and load IDTR */
void idt_init(void);

#endif /* CLAUDE_IDT_H */
