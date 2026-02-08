#if !defined(__i386__)
#error "ClaudeOS kernel must be compiled with an i686-elf cross-compiler"
#endif

#include "vga.h"
#include "serial.h"
#include "idt.h"
#include "irq.h"
#include "pit.h"
#include "pmm.h"
#include "heap.h"
#include "keyboard.h"
#include "console.h"
#include "process.h"

static void demo_process_a(void *arg)
{
    (void)arg;
    for (uint32_t i = 0; i < 3U; i++) {
        vga_puts("[PROC] demo process A tick.\n");
        serial_puts("[PROC] demo process A tick\n");
        process_yield();
    }
}

static void demo_process_b(void *arg)
{
    (void)arg;
    for (uint32_t i = 0; i < 3U; i++) {
        vga_puts("[PROC] demo process B tick.\n");
        serial_puts("[PROC] demo process B tick\n");
        process_yield();
    }
}

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

    kheap_init();
    vga_puts("Kernel heap initialized.\n");
    serial_puts("Kernel heap initialized\n");

    void *heap_test_a = kmalloc(64);
    void *heap_test_b = kmalloc(256);

    if (heap_test_a != 0 && heap_test_b != 0) {
        vga_puts("kmalloc/kfree self-test passed.\n");
        serial_puts("KHEAP: self-test passed\n");
    } else {
        vga_puts("kmalloc/kfree self-test failed.\n");
        serial_puts("KHEAP: self-test failed\n");
    }

    kfree(heap_test_b);
    kfree(heap_test_a);

    serial_puts("VGA initialized\n");

    idt_init();
    irq_init();
    vga_puts("IDT initialized.\n");
    serial_puts("IDT initialized\n");

    vga_puts("IRQ initialized.\n");
    serial_puts("IRQ initialized\n");

    pit_init();
    vga_puts("PIT initialized (100 Hz).\n");

    keyboard_init();
    vga_puts("PS/2 keyboard initialized (IRQ1).\n");
    serial_puts("Keyboard initialized\n");

    process_init();
    vga_puts("Process subsystem initialized.\n");
    serial_puts("Process subsystem initialized\n");

    (void)process_create_kernel("demo_a", demo_process_a, 0);
    (void)process_create_kernel("demo_b", demo_process_b, 0);

    process_run_ready();

    process_dump_table();

    console_init();

    /* Enable interrupts */
    __asm__ volatile ("sti");

    for (;;) {
        char c;

        while (keyboard_read_char(&c) != 0) {
            console_handle_char(c);
        }
        __asm__ volatile ("hlt");
    }
}
