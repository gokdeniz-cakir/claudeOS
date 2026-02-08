#include "idt.h"
#include "isr.h"

/* -----------------------------------------------------------------------
 * ISR entry points defined in isr_stubs.asm (isr0 through isr31)
 * ----------------------------------------------------------------------- */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

/* The IDT: 256 entries, initially zeroed (BSS) */
static struct idt_entry idt[IDT_ENTRIES];

/* The IDTR pointer loaded by lidt */
static struct idt_ptr idtr;

/* KERNEL_CS is defined in idt.h */

void idt_set_gate(uint8_t num, uint32_t offset, uint16_t selector,
                  uint8_t type_attr)
{
    idt[num].offset_low  = (uint16_t)(offset & 0xFFFF);
    idt[num].selector    = selector;
    idt[num].zero        = 0;
    idt[num].type_attr   = type_attr;
    idt[num].offset_high = (uint16_t)((offset >> 16) & 0xFFFF);
}

void idt_init(void)
{
    /* Set up the IDTR */
    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base  = (uint32_t)&idt;

    /* Install exception ISR handlers (vectors 0-31) */
    idt_set_gate(0,  (uint32_t)isr0,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(1,  (uint32_t)isr1,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(2,  (uint32_t)isr2,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(3,  (uint32_t)isr3,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(4,  (uint32_t)isr4,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(5,  (uint32_t)isr5,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(6,  (uint32_t)isr6,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(7,  (uint32_t)isr7,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(8,  (uint32_t)isr8,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(9,  (uint32_t)isr9,  KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(10, (uint32_t)isr10, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(11, (uint32_t)isr11, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(12, (uint32_t)isr12, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(13, (uint32_t)isr13, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(14, (uint32_t)isr14, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(15, (uint32_t)isr15, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(16, (uint32_t)isr16, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(17, (uint32_t)isr17, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(18, (uint32_t)isr18, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(19, (uint32_t)isr19, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(20, (uint32_t)isr20, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(21, (uint32_t)isr21, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(22, (uint32_t)isr22, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(23, (uint32_t)isr23, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(24, (uint32_t)isr24, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(25, (uint32_t)isr25, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(26, (uint32_t)isr26, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(27, (uint32_t)isr27, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(28, (uint32_t)isr28, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(29, (uint32_t)isr29, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(30, (uint32_t)isr30, KERNEL_CS, IDT_GATE_INT32);
    idt_set_gate(31, (uint32_t)isr31, KERNEL_CS, IDT_GATE_INT32);

    /* Vectors 32-47 left empty for now (IRQs, added in Task 7) */
    /* Vectors 48-255 left empty (available for software interrupts) */

    /* Load the IDT */
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}
