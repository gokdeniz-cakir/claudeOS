#include "pic.h"
#include "io.h"

#define PIC_READ_ISR    0x0B

static uint16_t pic_get_irq_reg(uint8_t ocw3)
{
    outb(PIC1_COMMAND, ocw3);
    outb(PIC2_COMMAND, ocw3);
    return (uint16_t)(((uint16_t)inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND));
}

void pic_init(void)
{
    /* ICW1: begin initialization sequence on both PICs */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    /* ICW2: set vector offsets */
    outb(PIC1_DATA, PIC1_OFFSET);   /* Master: IRQ 0-7 -> INT 0x20-0x27 */
    io_wait();
    outb(PIC2_DATA, PIC2_OFFSET);   /* Slave:  IRQ 8-15 -> INT 0x28-0x2F */
    io_wait();

    /* ICW3: configure cascading */
    outb(PIC1_DATA, 0x04);          /* Master: slave on IRQ2 (bit 2) */
    io_wait();
    outb(PIC2_DATA, 0x02);          /* Slave: cascade identity 2 */
    io_wait();

    /* ICW4: 8086 mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Mask all IRQs on both PICs.
     * Individual drivers will unmask their IRQ lines as needed. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(uint8_t irq)
{
    /* If the IRQ came from the slave PIC, send EOI to slave first */
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }

    /* Always send EOI to the master PIC */
    outb(PIC1_COMMAND, PIC_EOI);
}

uint16_t pic_get_isr(void)
{
    return pic_get_irq_reg(PIC_READ_ISR);
}

uint8_t pic_is_spurious_irq(uint8_t irq)
{
    uint16_t isr;

    if (irq == 7U) {
        isr = pic_get_isr();
        return (uint8_t)((isr & (1U << 7)) == 0U);
    }

    if (irq == 15U) {
        isr = pic_get_isr();
        if ((isr & (1U << 15)) == 0U) {
            /* For spurious IRQ15, only master gets EOI. */
            outb(PIC1_COMMAND, PIC_EOI);
            return 1U;
        }
    }

    return 0U;
}

void pic_clear_mask(uint8_t irq)
{
    uint16_t port;
    uint8_t mask;

    if (irq >= 16) {
        return;
    }

    if (irq < 8) {
        /* IRQ 0-7: master PIC */
        port = PIC1_DATA;
    } else {
        /* IRQ 8-15: slave PIC, adjust IRQ to 0-7 range */
        port = PIC2_DATA;
        irq -= 8;
        /* Also unmask IRQ2 (cascade) on master so slave IRQs can reach CPU */
        mask = inb(PIC1_DATA);
        mask &= ~(1 << 2);
        outb(PIC1_DATA, mask);
    }

    /* Read current mask, clear the bit for the requested IRQ, write back */
    mask = inb(port);
    mask &= ~(1 << irq);
    outb(port, mask);
}
