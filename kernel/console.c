#include "console.h"

#include <stdint.h>

#include "elf.h"
#include "serial.h"
#include "usermode.h"
#include "vga.h"

#define CONSOLE_PROMPT       "claudeos> "
#define CONSOLE_LINE_MAX     128u

static char console_line[CONSOLE_LINE_MAX];
static uint32_t console_len = 0;

static uint8_t console_line_equals(const char *literal)
{
    uint32_t i = 0U;

    if (literal == 0) {
        return 0U;
    }

    while (console_line[i] != '\0' && literal[i] != '\0') {
        if (console_line[i] != literal[i]) {
            return 0U;
        }
        i++;
    }

    return (uint8_t)(console_line[i] == '\0' && literal[i] == '\0');
}

static void console_print_prompt(void)
{
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts(CONSOLE_PROMPT);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void console_execute_line(void)
{
    if (console_line_equals("help") != 0U) {
        vga_puts("Commands: help, ring3test, elftest, forkexec, libctest, shell\n");
        serial_puts("[CONSOLE] help shown\n");
        return;
    }

    if (console_line_equals("ring3test") != 0U) {
        usermode_run_ring3_test();
        return;
    }

    if (console_line_equals("elftest") != 0U) {
        elf_run_embedded_test();
        return;
    }

    if (console_line_equals("forkexec") != 0U) {
        elf_run_fork_exec_test();
        return;
    }

    if (console_line_equals("libctest") != 0U) {
        elf_run_libc_test();
        return;
    }

    if (console_line_equals("shell") != 0U) {
        elf_run_shell();
        return;
    }
}

void console_init(void)
{
    console_len = 0;
    vga_puts("\nConsole ready. Type and press Enter.\n");
    vga_puts("Type 'help' for commands.\n");
    console_print_prompt();
    serial_puts("[CONSOLE] Initialized\n");
}

void console_handle_char(char c)
{
    if (c == '\r') {
        return;
    }

    if (c == '\n') {
        console_line[console_len] = '\0';
        vga_putchar('\n');

        serial_puts("[CONSOLE] ");
        serial_puts(console_line);
        serial_puts("\n");

        console_execute_line();

        console_len = 0;
        console_print_prompt();
        return;
    }

    if (c == '\b') {
        if (console_len == 0u) {
            return;
        }

        console_len--;
        console_line[console_len] = '\0';
        vga_putchar('\b');
        return;
    }

    if (c == '\t') {
        c = ' ';
    }

    if ((c < 32) || (c > 126)) {
        return;
    }

    if (console_len >= (CONSOLE_LINE_MAX - 1u)) {
        return;
    }

    console_line[console_len] = c;
    console_len++;
    vga_putchar(c);
}
