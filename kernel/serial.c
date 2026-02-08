#include "serial.h"
#include "io.h"
#include <stdint.h>

#define COM1_BASE   0x3F8
#define REG_DATA    (COM1_BASE + 0)
#define REG_IER     (COM1_BASE + 1)
#define REG_DLL     (COM1_BASE + 0)
#define REG_DLH     (COM1_BASE + 1)
#define REG_FCR     (COM1_BASE + 2)
#define REG_LCR     (COM1_BASE + 3)
#define REG_MCR     (COM1_BASE + 4)
#define REG_LSR     (COM1_BASE + 5)

#define LCR_DLAB    0x80
#define LCR_8N1     0x03
#define LSR_THRE    0x20
#define FCR_ENABLE       0x01
#define FCR_CLR_RX       0x02
#define FCR_CLR_TX       0x04
#define FCR_TRIGGER_14   0xC0
#define MCR_DTR     0x01
#define MCR_RTS     0x02
#define MCR_OUT2    0x08
#define BAUD_38400_DIVISOR  3

void serial_init(void)
{
    outb(REG_IER, 0x00);
    outb(REG_LCR, LCR_DLAB);
    outb(REG_DLL, (uint8_t)(BAUD_38400_DIVISOR & 0xFF));
    outb(REG_DLH, (uint8_t)((BAUD_38400_DIVISOR >> 8) & 0xFF));
    outb(REG_LCR, LCR_8N1);
    outb(REG_FCR, FCR_ENABLE | FCR_CLR_RX | FCR_CLR_TX | FCR_TRIGGER_14);
    outb(REG_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);
}

void serial_putchar(char c)
{
    while ((inb(REG_LSR) & LSR_THRE) == 0) {}
    outb(REG_DATA, (uint8_t)c);
}

void serial_puts(const char *str)
{
    while (*str) {
        if (*str == '\n') {
            serial_putchar('\r');
        }
        serial_putchar(*str);
        str++;
    }
}
