#ifndef CLAUDE_PIT_H
#define CLAUDE_PIT_H

#include <stdint.h>

/* 8253/8254 PIT I/O ports */
#define PIT_CHANNEL0_DATA   0x40    /* Channel 0 data port (read/write) */
#define PIT_COMMAND         0x43    /* Mode/Command register (write only) */

/* PIT oscillator base frequency in Hz */
#define PIT_BASE_FREQ       1193182

/* Target tick frequency in Hz (10ms per tick) */
#define PIT_TARGET_FREQ     100

/* Divisor to achieve target frequency: 1193182 / 100 = 11931 (0x2E9B) */
#define PIT_DIVISOR         (PIT_BASE_FREQ / PIT_TARGET_FREQ)

/* Mode/Command byte: channel 0, lobyte/hibyte, mode 2 (rate generator), binary */
#define PIT_CMD_CH0_MODE2   0x34

/*
 * Initialize the PIT.
 * Programs channel 0 in mode 2 (rate generator) at 100 Hz and
 * registers the IRQ0 handler. Must be called before enabling interrupts.
 */
void pit_init(void);

/*
 * Return the number of ticks elapsed since PIT initialization.
 * Each tick represents 10ms (at 100 Hz).
 */
uint32_t pit_get_ticks(void);

#endif /* CLAUDE_PIT_H */
