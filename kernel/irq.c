#include "irq.h"
#include "pic.h"
#include "idt.h"

/* Dispatch table: one handler slot per IRQ line (0-15) */
static irq_handler_t irq_handlers[IRQ_COUNT];

/* External symbols from irq_stubs.asm */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

void irq_register_handler(uint8_t irq, irq_handler_t handler)
{
    if (irq < IRQ_COUNT) {
        irq_handlers[irq] = handler;
    }
}

void irq_handler(struct isr_regs *regs)
{
    /* Convert interrupt number back to IRQ number (0-15) */
    uint8_t irq = (uint8_t)(regs->int_no - IRQ_BASE);

    if (irq >= IRQ_COUNT) {
        return;
    }

    /* Handle PIC spurious IRQ7/IRQ15 before dispatching/EOIing as normal. */
    if ((irq == 7U || irq == 15U) && pic_is_spurious_irq(irq) != 0U) {
        return;
    }

    /* Dispatch to registered handler if one exists */
    if (irq_handlers[irq] != 0) {
        irq_handlers[irq](regs);
    }

    /* Send EOI to PIC after handling */
    pic_send_eoi(irq);
}

void irq_init(void)
{
    /* Remap the PIC */
    pic_init();

    /* Install IDT gates for IRQ 0-15 (INT 32-47) */
    idt_set_gate(32, (uint32_t)irq0,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(33, (uint32_t)irq1,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(34, (uint32_t)irq2,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(35, (uint32_t)irq3,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(36, (uint32_t)irq4,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(37, (uint32_t)irq5,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(38, (uint32_t)irq6,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(39, (uint32_t)irq7,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(40, (uint32_t)irq8,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(41, (uint32_t)irq9,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(42, (uint32_t)irq10, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(43, (uint32_t)irq11, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(44, (uint32_t)irq12, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(45, (uint32_t)irq13, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(46, (uint32_t)irq14, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(47, (uint32_t)irq15, KERNEL_CS, IDT_GATE_INT32);
}
