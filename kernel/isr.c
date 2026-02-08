#include "isr.h"
#include "vga.h"
#include "serial.h"

/* Human-readable exception names for vectors 0-31 */
static const char *exception_names[32] = {
    "Division Error",                   /*  0 */
    "Debug",                            /*  1 */
    "Non-Maskable Interrupt",           /*  2 */
    "Breakpoint",                       /*  3 */
    "Overflow",                         /*  4 */
    "Bound Range Exceeded",             /*  5 */
    "Invalid Opcode",                   /*  6 */
    "Device Not Available",             /*  7 */
    "Double Fault",                     /*  8 */
    "Coprocessor Segment Overrun",      /*  9 */
    "Invalid TSS",                      /* 10 */
    "Segment Not Present",              /* 11 */
    "Stack-Segment Fault",              /* 12 */
    "General Protection Fault",         /* 13 */
    "Page Fault",                       /* 14 */
    "Reserved",                         /* 15 */
    "x87 Floating-Point Exception",     /* 16 */
    "Alignment Check",                  /* 17 */
    "Machine Check",                    /* 18 */
    "SIMD Floating-Point Exception",    /* 19 */
    "Virtualization Exception",         /* 20 */
    "Control Protection Exception",     /* 21 */
    "Reserved",                         /* 22 */
    "Reserved",                         /* 23 */
    "Reserved",                         /* 24 */
    "Reserved",                         /* 25 */
    "Reserved",                         /* 26 */
    "Reserved",                         /* 27 */
    "Hypervisor Injection Exception",   /* 28 */
    "VMM Communication Exception",      /* 29 */
    "Security Exception",               /* 30 */
    "Reserved",                         /* 31 */
};

/* Convert a hex nibble (0-15) to its ASCII character */
static char hex_char(uint8_t nibble)
{
    return (nibble < 10) ? ('0' + (char)nibble) : ('A' + (char)(nibble - 10));
}

/* Print a 32-bit value as "0xXXXXXXXX" to both VGA and serial */
static void print_hex32(uint32_t val)
{
    char buf[11]; /* "0x" + 8 hex chars + NUL */
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex_char((uint8_t)((val >> (i * 4)) & 0xF));
    }
    buf[10] = '\0';
    vga_puts(buf);
    serial_puts(buf);
}

/* Print a label followed by a hex value and newline */
static void print_reg(const char *label, uint32_t val)
{
    vga_puts(label);
    serial_puts(label);
    print_hex32(val);
    vga_puts("\n");
    serial_puts("\n");
}

void isr_handler(struct isr_regs *regs)
{
    /* Set error colors: white on red */
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);

    vga_puts("\n*** EXCEPTION: ");
    serial_puts("\n*** EXCEPTION: ");

    if (regs->int_no < 32) {
        vga_puts(exception_names[regs->int_no]);
        serial_puts(exception_names[regs->int_no]);
    } else {
        vga_puts("Unknown");
        serial_puts("Unknown");
    }

    vga_puts(" ***\n");
    serial_puts(" ***\n");

    /* Print interrupt number and error code */
    print_reg("  INT:    ", regs->int_no);
    print_reg("  ERR:    ", regs->err_code);

    /* Print CPU registers */
    print_reg("  EAX:    ", regs->eax);
    print_reg("  EBX:    ", regs->ebx);
    print_reg("  ECX:    ", regs->ecx);
    print_reg("  EDX:    ", regs->edx);
    print_reg("  ESI:    ", regs->esi);
    print_reg("  EDI:    ", regs->edi);
    print_reg("  EBP:    ", regs->ebp);
    print_reg("  EIP:    ", regs->eip);
    print_reg("  CS:     ", regs->cs);
    print_reg("  EFLAGS: ", regs->eflags);
    print_reg("  DS:     ", regs->ds);
    print_reg("  ES:     ", regs->es);
    print_reg("  FS:     ", regs->fs);
    print_reg("  GS:     ", regs->gs);

    vga_puts("\nSystem halted.\n");
    serial_puts("\nSystem halted.\n");

    /* Halt the CPU */
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
