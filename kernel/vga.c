#include "vga.h"
#include "io.h"

static volatile uint16_t *vga_buffer = (volatile uint16_t *)VGA_MEMORY;
static uint16_t vga_row;
static uint16_t vga_col;
static uint8_t  vga_attrib;

static inline uint16_t vga_entry(char c, uint8_t attrib)
{
    return (uint16_t)(unsigned char)c | ((uint16_t)attrib << 8);
}

static void vga_update_cursor(void)
{
    uint16_t pos = vga_row * VGA_WIDTH + vga_col;
    outb(VGA_CRTC_INDEX, VGA_CURSOR_LOW);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
    outb(VGA_CRTC_INDEX, VGA_CURSOR_HIGH);
    outb(VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_enable_cursor(void)
{
    outb(VGA_CRTC_INDEX, 0x0A);
    outb(VGA_CRTC_DATA, (inb(VGA_CRTC_DATA) & 0xC0) | 13);
    outb(VGA_CRTC_INDEX, 0x0B);
    outb(VGA_CRTC_DATA, (inb(VGA_CRTC_DATA) & 0xE0) | 15);
}

static void vga_scroll(void)
{
    uint16_t i;
    for (i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }
    uint16_t blank = vga_entry(' ', vga_attrib);
    for (i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        vga_buffer[i] = blank;
    }
}

static void vga_clamp_cursor(void)
{
    if (vga_row >= VGA_HEIGHT) {
        vga_row = (uint16_t)(VGA_HEIGHT - 1);
    }
    if (vga_col >= VGA_WIDTH) {
        vga_col = (uint16_t)(VGA_WIDTH - 1);
    }
}

void vga_init(void)
{
    vga_row = 0;
    vga_col = 0;
    vga_attrib = VGA_COLOR_LIGHT_GREY | (VGA_COLOR_BLACK << 4);
    vga_clear();
    vga_enable_cursor();
    vga_update_cursor();
}

void vga_clear(void)
{
    uint16_t blank = vga_entry(' ', vga_attrib);
    uint16_t i;

    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }

    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
}

void vga_set_cursor(uint16_t row, uint16_t col)
{
    vga_row = row;
    vga_col = col;
    vga_clamp_cursor();
    vga_update_cursor();
}

void vga_get_cursor(uint16_t *row, uint16_t *col)
{
    if (row != 0) {
        *row = vga_row;
    }
    if (col != 0) {
        *col = vga_col;
    }
}

void vga_putchar(char c)
{
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
        } else if (vga_row > 0) {
            vga_row--;
            vga_col = (uint16_t)(VGA_WIDTH - 1);
        }

        if (vga_row < VGA_HEIGHT && vga_col < VGA_WIDTH) {
            uint16_t offset = vga_row * VGA_WIDTH + vga_col;
            vga_buffer[offset] = vga_entry(' ', vga_attrib);
        }
    } else if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    } else {
        uint16_t offset = vga_row * VGA_WIDTH + vga_col;
        vga_buffer[offset] = vga_entry(c, vga_attrib);
        vga_col++;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    }
    while (vga_row >= VGA_HEIGHT) {
        vga_scroll();
        vga_row--;
    }
    vga_update_cursor();
}

void vga_puts(const char *str)
{
    while (*str) {
        vga_putchar(*str);
        str++;
    }
}

void vga_set_color(uint8_t fg, uint8_t bg)
{
    vga_attrib = (fg & 0x0F) | ((bg & 0x0F) << 4);
}
