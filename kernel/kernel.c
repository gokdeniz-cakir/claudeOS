#if !defined(__i386__)
#error "ClaudeOS kernel must be compiled with an i686-elf cross-compiler"
#endif

#include "vga.h"
#include "serial.h"
#include "idt.h"
#include "irq.h"
#include "pit.h"
#include "pmm.h"

void kernel_main(void)
{
    serial_init();
    serial_puts("ClaudeOS serial debug ready\n");
    serial_puts("Paging enabled\n");

    pmm_init();

    vga_init();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_puts("ClaudeOS");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts(" v0.1\n\n");
    vga_puts("Kernel loaded successfully.\n");
    vga_puts("Paging enabled.\n");
    vga_puts("PMM initialized.\n");

    serial_puts("VGA initialized\n");

    idt_init();
    irq_init();
    vga_puts("IDT initialized.\n");
    serial_puts("IDT initialized\n");

    vga_puts("IRQ initialized.\n");
    serial_puts("IRQ initialized\n");

    pit_init();
    vga_puts("PIT initialized (100 Hz).\n");

    /* Enable interrupts */
    __asm__ volatile ("sti");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
