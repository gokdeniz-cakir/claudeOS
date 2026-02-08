#include "console.h"

#include <stdint.h>

#include "serial.h"
#include "vga.h"

#define CONSOLE_PROMPT       "claudeos> "
#define CONSOLE_LINE_MAX     128u

static char console_line[CONSOLE_LINE_MAX];
static uint32_t console_len = 0;

static void console_print_prompt(void)
{
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts(CONSOLE_PROMPT);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void console_init(void)
{
    console_len = 0;
    vga_puts("\nConsole ready. Type and press Enter.\n");
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
