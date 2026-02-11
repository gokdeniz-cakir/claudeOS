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
#include "mouse.h"
#include "console.h"
#include "process.h"
#include "tss.h"
#include "syscall.h"
#include "vfs.h"
#include "initrd.h"
#include "fat32.h"
#include "vbe.h"
#include "wm.h"

static void demo_delay(void)
{
    volatile uint32_t i;
    for (i = 0U; i < 20000000U; i++) {
        __asm__ volatile ("" : : : "memory");
    }
}

static void demo_process_a(void *arg)
{
    (void)arg;
    for (uint32_t i = 0; i < 3U; i++) {
        vga_puts("[PROC] demo process A tick.\n");
        serial_puts("[PROC] demo process A tick\n");
        demo_delay();
    }
}

static void demo_process_b(void *arg)
{
    (void)arg;
    for (uint32_t i = 0; i < 3U; i++) {
        vga_puts("[PROC] demo process B tick.\n");
        serial_puts("[PROC] demo process B tick\n");
        demo_delay();
    }
}

void kernel_main(void)
{
    serial_init();
    serial_puts("ClaudeOS serial debug ready\n");
    serial_puts("Paging enabled\n");

    pmm_init();
    kheap_init();
    (void)vbe_init();

    vga_init();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_puts("ClaudeOS");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts(" v0.1\n\n");
    vga_puts("Kernel loaded successfully.\n");
    vga_puts("Paging enabled.\n");
    vga_puts("PMM initialized.\n");

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

    mouse_init();
    if (mouse_is_initialized() != 0) {
        vga_puts("PS/2 mouse initialized (IRQ12).\n");
    } else {
        vga_puts("PS/2 mouse init failed.\n");
    }

    if (wm_init() == 0) {
        vga_puts("Window manager ready (type wmstart).\n");
    } else {
        vga_puts("Window manager unavailable.\n");
    }

    process_init();
    vga_puts("Process subsystem initialized.\n");
    serial_puts("Process subsystem initialized\n");

    syscall_init();
    vga_puts("INT 0x80 syscall interface initialized.\n");

    vfs_init();
    vga_puts("VFS initialized.\n");
    serial_puts("VFS initialized\n");

    if (initrd_init() == 0) {
        vga_puts("Initrd mounted.\n");
    } else {
        vga_puts("Initrd mount failed.\n");
    }

    if (fat32_init() == 0) {
        vga_puts("FAT32 mounted at /fat.\n");
    } else {
        vga_puts("FAT32 mount failed.\n");
    }

    tss_init();
    vga_puts("TSS initialized.\n");
    serial_puts("TSS initialized\n");
    process_refresh_tss_stack();
    serial_puts("[PROC] TSS esp0 synchronized for current task\n");

    (void)process_create_kernel("demo_a", demo_process_a, 0);
    (void)process_create_kernel("demo_b", demo_process_b, 0);

    process_set_preemption(1U);
    serial_puts("[PROC] Preemptive scheduler enabled (PIT-driven)\n");

    console_init();

    /* Enable interrupts */
    __asm__ volatile ("sti");

    uint8_t demo_summary_printed = 0U;

    for (;;) {
        char c;

        if (demo_summary_printed == 0U && process_count() == 1U) {
            process_dump_table();
            demo_summary_printed = 1U;
        }

        while (keyboard_read_char(&c) != 0) {
            if (wm_is_active() != 0) {
                if (c == 0x1B) {
                    wm_stop();
                    vga_clear();
                    vga_puts("Window manager stopped.\n");
                    console_show_prompt();
                } else {
                    wm_handle_key(c);
                }
                continue;
            }

            console_handle_char(c);
        }

        if (wm_is_active() != 0) {
            wm_update();
        }
        __asm__ volatile ("hlt");
    }
}
