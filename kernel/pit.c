#include "pit.h"
#include "io.h"
#include "irq.h"
#include "pic.h"
#include "serial.h"

/* Global tick counter — volatile because it is modified in interrupt context */
static volatile uint32_t pit_ticks = 0;

/*
 * IRQ0 handler: increment the system tick counter.
 * This runs in interrupt context and must be fast.
 * EOI is sent by the IRQ dispatch code in irq_handler(), so we do not
 * send it here.
 */
static void pit_handler(struct isr_regs *regs)
{
    (void)regs;  /* Unused */
    pit_ticks++;
}

/*
 * Return the current tick count.
 */
uint32_t pit_get_ticks(void)
{
    return pit_ticks;
}

/*
 * Initialize the Programmable Interval Timer.
 * Programs channel 0 in mode 2 (rate generator) with a divisor of 11931,
 * producing an interrupt at approximately 100 Hz (every 10ms).
 * Registers the IRQ0 handler and unmasks IRQ0 on the PIC.
 */
void pit_init(void)
{
    /* Send the mode/command byte: channel 0, lobyte/hibyte, mode 2, binary */
    outb(PIT_COMMAND, PIT_CMD_CH0_MODE2);

    /* Send the divisor as two bytes (lobyte first, then hibyte) */
    outb(PIT_CHANNEL0_DATA, (uint8_t)(PIT_DIVISOR & 0xFF));        /* Low byte */
    outb(PIT_CHANNEL0_DATA, (uint8_t)((PIT_DIVISOR >> 8) & 0xFF)); /* High byte */

    /* Register our handler for IRQ0 (timer) */
    irq_register_handler(0, pit_handler);

    /* Unmask IRQ0 on the master PIC so timer interrupts are delivered */
    pic_clear_mask(0);

    serial_puts("[PIT] Initialized at 100 Hz (divisor=11931)\n");
}
