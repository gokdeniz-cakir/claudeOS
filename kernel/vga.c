#include "vga.h"

#include "fb.h"
#include "io.h"
#include "serial.h"

static volatile uint16_t *vga_buffer = (volatile uint16_t *)VGA_MEMORY;
static uint16_t vga_row;
static uint16_t vga_col;
static uint8_t vga_attrib;
static uint8_t vga_use_framebuffer;

static inline uint16_t vga_entry(char c, uint8_t attrib)
{
    return (uint16_t)(uint8_t)c | ((uint16_t)attrib << 8);
}

static void vga_update_cursor_hw(void)
{
    uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
    outb(VGA_CRTC_INDEX, VGA_CURSOR_LOW);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFFU));
    outb(VGA_CRTC_INDEX, VGA_CURSOR_HIGH);
    outb(VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFFU));
}

static void vga_enable_cursor_hw(void)
{
    outb(VGA_CRTC_INDEX, 0x0AU);
    outb(VGA_CRTC_DATA, (uint8_t)((inb(VGA_CRTC_DATA) & 0xC0U) | 13U));
    outb(VGA_CRTC_INDEX, 0x0BU);
    outb(VGA_CRTC_DATA, (uint8_t)((inb(VGA_CRTC_DATA) & 0xE0U) | 15U));
}

static void vga_scroll_hw(void)
{
    for (uint16_t i = 0; i < (uint16_t)((VGA_HEIGHT - 1U) * VGA_WIDTH); i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }

    uint16_t blank = vga_entry(' ', vga_attrib);
    for (uint16_t i = (uint16_t)((VGA_HEIGHT - 1U) * VGA_WIDTH); i < (VGA_WIDTH * VGA_HEIGHT); i++) {
        vga_buffer[i] = blank;
    }
}

static void vga_clamp_cursor_hw(void)
{
    if (vga_row >= VGA_HEIGHT) {
        vga_row = (uint16_t)(VGA_HEIGHT - 1U);
    }
    if (vga_col >= VGA_WIDTH) {
        vga_col = (uint16_t)(VGA_WIDTH - 1U);
    }
}

static inline uint8_t vga_fg(void)
{
    return (uint8_t)(vga_attrib & 0x0FU);
}

static inline uint8_t vga_bg(void)
{
    return (uint8_t)((vga_attrib >> 4) & 0x0FU);
}

void vga_init(void)
{
    vga_row = 0U;
    vga_col = 0U;
    vga_attrib = (uint8_t)(VGA_COLOR_LIGHT_GREY | (VGA_COLOR_BLACK << 4));
    vga_use_framebuffer = 0U;

    if (fb_init() == 0) {
        vga_use_framebuffer = 1U;
        fb_console_init(vga_fg(), vga_bg());
        serial_puts("[VGA] framebuffer console active\n");
        return;
    }

    for (uint16_t i = 0; i < (VGA_WIDTH * VGA_HEIGHT); i++) {
        vga_buffer[i] = vga_entry(' ', vga_attrib);
    }
    vga_enable_cursor_hw();
    vga_update_cursor_hw();
}

void vga_clear(void)
{
    if (vga_use_framebuffer != 0U) {
        fb_console_init(vga_fg(), vga_bg());
        return;
    }

    uint16_t blank = vga_entry(' ', vga_attrib);
    for (uint16_t i = 0; i < (VGA_WIDTH * VGA_HEIGHT); i++) {
        vga_buffer[i] = blank;
    }

    vga_row = 0U;
    vga_col = 0U;
    vga_update_cursor_hw();
}

void vga_set_cursor(uint16_t row, uint16_t col)
{
    if (vga_use_framebuffer != 0U) {
        fb_console_set_cursor(row, col);
        return;
    }

    vga_row = row;
    vga_col = col;
    vga_clamp_cursor_hw();
    vga_update_cursor_hw();
}

void vga_get_cursor(uint16_t *row, uint16_t *col)
{
    if (vga_use_framebuffer != 0U) {
        fb_console_get_cursor(row, col);
        return;
    }

    if (row != 0) {
        *row = vga_row;
    }
    if (col != 0) {
        *col = vga_col;
    }
}

void vga_putchar(char c)
{
    if (vga_use_framebuffer != 0U) {
        fb_console_putchar(c, vga_fg(), vga_bg());
        return;
    }

    if (c == '\n') {
        vga_col = 0U;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0U;
    } else if (c == '\b') {
        if (vga_col > 0U) {
            vga_col--;
        } else if (vga_row > 0U) {
            vga_row--;
            vga_col = (uint16_t)(VGA_WIDTH - 1U);
        }

        if (vga_row < VGA_HEIGHT && vga_col < VGA_WIDTH) {
            uint16_t offset = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
            vga_buffer[offset] = vga_entry(' ', vga_attrib);
        }
    } else if (c == '\t') {
        vga_col = (uint16_t)((vga_col + 8U) & ~7U);
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0U;
            vga_row++;
        }
    } else {
        uint16_t offset = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
        vga_buffer[offset] = vga_entry(c, vga_attrib);
        vga_col++;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0U;
            vga_row++;
        }
    }

    while (vga_row >= VGA_HEIGHT) {
        vga_scroll_hw();
        vga_row--;
    }
    vga_update_cursor_hw();
}

void vga_puts(const char *str)
{
    while (*str != '\0') {
        vga_putchar(*str);
        str++;
    }
}

void vga_set_color(uint8_t fg, uint8_t bg)
{
    vga_attrib = (uint8_t)((fg & 0x0FU) | ((bg & 0x0FU) << 4));
}
